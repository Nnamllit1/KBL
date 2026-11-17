"""Prepare an EFI-stub kernel and optional initrd/DTB without building a flash image."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile

LIMIT = 256 * 1024 * 1024

def prepare(args):
    components = {}
    header = None
    vendor_header = None
    def inspect(path):
        if path.stat().st_size > LIMIT:
            raise ValueError('input exceeds 256 MiB limit')
        p = subprocess.run([str(args.inspector.resolve()), 'inspect-json', str(path)],
                           capture_output=True, text=True, check=True)
        return json.loads(p.stdout), path.read_bytes()
    if args.boot_image:
        if not args.inspector:
            raise ValueError('--boot-image requires the native KBL --inspector executable')
        header, raw = inspect(args.boot_image)
        if header['kind'] != 'boot':
            raise ValueError('--boot-image must be a BOOT image, not vendor_boot')
        for name in ('kernel', 'ramdisk', 'dtb', 'second'):
            region = header[name]
            if region['size']:
                components[name] = raw[region['offset']:region['offset'] + region['size']]
        if components.get('second'):
            raise ValueError('second-stage Android boot payload is not supported')
        if header['version'] >= 3 and not args.vendor_boot:
            raise ValueError('boot v3/v4 requires its matching --vendor-boot image')
        if args.vendor_boot:
            vendor_header, vendor = inspect(args.vendor_boot)
            if header['version'] < 3 or vendor_header['kind'] != 'vendor_boot' or header['version'] != vendor_header['version']:
                raise ValueError('boot and vendor_boot header versions must match')
            for name, key in [('ramdisk', 'vendor_ramdisk'), ('dtb', 'dtb'), ('bootconfig', 'bootconfig')]:
                region = vendor_header.get(name, {'size': 0})
                if region['size']:
                    components[key] = vendor[region['offset']:region['offset'] + region['size']]
    else:
        for name, path in [('kernel', args.kernel), ('ramdisk', args.ramdisk), ('dtb', args.dtb)]:
            if path:
                if path.stat().st_size > LIMIT:
                    raise ValueError(name + ' exceeds 256 MiB limit')
                components[name] = path.read_bytes()
    if not components.get('kernel'):
        raise ValueError('image does not contain a kernel')
    kernel = components['kernel']
    source_sha = hashlib.sha256(kernel).hexdigest()
    if kernel.startswith(b'\x1f\x8b'):
        with gzip.GzipFile(fileobj=io.BytesIO(kernel)) as compressed:
            kernel = compressed.read(LIMIT + 1)
        if len(kernel) > LIMIT:
            raise ValueError('decompressed kernel exceeds limit')
    pe = struct.unpack_from('<I', kernel, 60)[0] if len(kernel) >= 64 else len(kernel)
    if (kernel[:2] != b'MZ' or pe + 94 > len(kernel) or kernel[pe:pe+4] != b'PE\0\0'
            or kernel[pe+4:pe+6] != b'\x64\xaa'
            or struct.unpack_from('<H', kernel, pe+24+68)[0] != 10):
        raise ValueError('kernel has no ARM64 EFI application entry point')
    components['kernel'] = kernel
    if components.get('dtb'):
        if not args.inspector:
            raise ValueError('--dtb requires --inspector to validate and select the flattened device tree')
        with tempfile.TemporaryDirectory(prefix='kbl-dtb-') as folder:
            source = Path(folder) / 'input.dtb'
            selected = Path(folder) / 'selected.dtb'
            source.write_bytes(components['dtb'])
            subprocess.run([str(args.inspector.resolve()), 'extract-dtb', str(source), str(selected), str(args.dtb_index)],
                           check=True, capture_output=True)
            components['dtb'] = selected.read_bytes()
    args.out.mkdir(parents=True, exist_ok=False)
    files = {}
    generic_name = 'generic-initramfs' if vendor_header else 'initramfs'
    for name, destination in [('kernel', 'Image.efi'), ('ramdisk', generic_name), ('dtb', 'vendor.dtb'),
                              ('vendor_ramdisk', 'vendor-initramfs'), ('bootconfig', 'bootconfig')]:
        if name in components:
            data = components[name]
            (args.out / destination).write_bytes(data)
            files[destination] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    result = {
        'kernel_source_sha256': source_sha, 'files': files,
        'android_header': header,
        'vendor_header': vendor_header,
        'ramdisks_combined': False,
        'flashable': False, 'device_boot_validated': False,
        'note': 'Payload preparation only. Device UEFI port and Android boot context are required.'
    }
    (args.out / 'payload.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Prepared ARM64 EFI payload; this is not a tablet flash package:', args.out)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--boot-image', type=Path)
    source.add_argument('--kernel', type=Path)
    parser.add_argument('--vendor-boot', type=Path)
    parser.add_argument('--inspector', type=Path)
    parser.add_argument('--ramdisk', type=Path)
    parser.add_argument('--dtb', type=Path)
    parser.add_argument('--dtb-index', type=int, default=0)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    if args.boot_image and (args.ramdisk or args.dtb):
        parser.error('raw boot-image extraction cannot be combined with separate ramdisk/DTB inputs')
    if args.vendor_boot and not args.boot_image:
        parser.error('--vendor-boot requires --boot-image')
    prepare(args)
