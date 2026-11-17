"""Create an SM-X110 CYE5 BOOT-only probe and exact-input BOOT restore archive."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import tarfile

ROOT = Path(__file__).resolve().parents[1]
KNOWN_CYE5_BOOTS = {
    'd50e78b8d4e6c9aeb5a7b849d7a71552c00e1636b7a444e073a7300944865784',
    'ea45f76ac5be10765fb4573ba064a5abd88f6837c740c9abc6d24ae177301784',
}
PARTITION_SIZE = 67108864

def sha(data): return hashlib.sha256(data).hexdigest()
def align(n): return (n + 4095) & ~4095

def parse(raw):
    if len(raw) != PARTITION_SIZE or raw[:8] != b'ANDROID!':
        raise ValueError('Expected a raw 64 MiB CYE5 BOOT image')
    k, r, os_version, header_size = struct.unpack_from('<IIII', raw, 8)
    if (struct.unpack_from('<I', raw, 40)[0] != 4 or header_size != 1584 or
            os_version != 402653588 or any(raw[24:40])):
        raise ValueError('Unexpected CYE5 boot header or OS field')
    s = struct.unpack_from('<I', raw, 1580)[0]
    if s != 4096: raise ValueError('Unexpected GKI signature slot')
    ramdisk = 4096 + align(k)
    signature = ramdisk + align(r)
    marker = signature + align(s)
    if marker + 528 >= len(raw) - 64 or raw[marker:marker+16] != b'SEANDROIDENFORCE':
        raise ValueError('Missing structurally located Samsung metadata')
    if raw[marker+16:marker+27] != b'SignerVer02' or any(raw[signature:signature+s]):
        raise ValueError('Unexpected Samsung signer or GKI signature data')
    if raw[-64:-60] != b'AVBf': raise ValueError('Missing original AVB footer')
    original, vb_offset, vb_size = struct.unpack_from('>QQQ', raw, len(raw)-52)
    if (original != marker + 528 or vb_offset != align(original) or vb_size < 256 or
            vb_offset + vb_size >= len(raw)-64):
        raise ValueError('Unexpected AVB layout')
    vb = raw[vb_offset:vb_offset+vb_size]
    if vb[:4] != b'AVB0' or struct.unpack_from('>QII', vb, 112) != (0, 0, 0):
        raise ValueError('Unexpected AVB rollback index, location or flags')
    if any(raw[vb_offset+vb_size:-64]): raise ValueError('Unexpected image trailer')
    return {'header': raw[:4096], 'kernel': raw[4096:4096+k], 'ramdisk': raw[ramdisk:ramdisk+r],
            'signature': raw[signature:signature+s], 'samsung': raw[marker:marker+528],
            'vbmeta': vb, 'footer': raw[-64:]}

def replace_kernel(source, kernel):
    parts = parse(source)
    new = bytearray(PARTITION_SIZE)
    new[:4096] = parts['header']
    struct.pack_into('<I', new, 8, len(kernel))
    pos = 4096
    for component in (kernel, parts['ramdisk'], parts['signature']):
        new[pos:pos+len(component)] = component
        pos += align(len(component))
    new[pos:pos+528] = parts['samsung']
    original_size = pos + 528
    vb_offset = align(original_size)
    if vb_offset + len(parts['vbmeta']) >= PARTITION_SIZE - 64:
        raise ValueError('Probe exceeds BOOT partition')
    new[vb_offset:vb_offset+len(parts['vbmeta'])] = parts['vbmeta']
    new[-64:] = parts['footer']
    struct.pack_into('>QQQ', new, PARTITION_SIZE-52, original_size, vb_offset, len(parts['vbmeta']))
    result = bytes(new)
    check = parse(result)
    for key in ('ramdisk', 'signature', 'samsung', 'vbmeta'):
        if check[key] != parts[key]: raise ValueError('Unexpected mutation: ' + key)
    if result[:8] != source[:8] or result[12:4096] != source[12:4096]:
        raise ValueError('Header mutation escaped kernel_size')
    return result, parts

def archive(output, raw, filename):
    output.mkdir(parents=True, exist_ok=False)
    destination = output / filename
    with tarfile.open(destination, 'w', format=tarfile.USTAR_FORMAT) as tar:
        member = tarfile.TarInfo('boot.img')
        member.size = len(raw); member.mode = 0o644; member.mtime = 0
        tar.addfile(member, io.BytesIO(raw))
    with destination.open('rb') as stream: md5 = hashlib.file_digest(stream, 'md5').hexdigest()
    with destination.open('ab') as stream: stream.write((md5 + '  ' + filename[:-4] + '\n').encode('ascii'))
    with tarfile.open(destination) as tar:
        members = tar.getmembers()
        if len(members) != 1 or members[0].name != 'boot.img' or tar.extractfile(members[0]).read() != raw:
            raise ValueError('Archive round-trip failed')
    with destination.open('rb') as stream: digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    (output / 'SHA256SUMS.txt').write_text(digest + '  ' + filename + '\n')
    return {'filename': filename, 'sha256': digest, 'boot_image_sha256': sha(raw), 'members': ['boot.img']}

def package(args):
    if args.out.exists(): raise ValueError('Use a new output directory')
    if args.source_boot.stat().st_size != PARTITION_SIZE: raise ValueError('Unexpected source image size')
    source = args.source_boot.read_bytes()
    if sha(source) not in KNOWN_CYE5_BOOTS:
        raise ValueError('Source must match an audited original or rooted CYE5 BOOT image')
    build = json.loads((args.build / 'build.json').read_text())
    image = (args.build / 'Image').read_bytes()
    kind = build['kind']
    if kind not in ('bare-metal-display-probe', 'pre-kernel-entry-control', 'pre-kernel-display-bridge', 'pre-kernel-boot-menu'):
        raise ValueError('Unknown payload kind')
    if (build['qemu_uart_enabled'] or sha(image) != build['image_sha256'] or
            build.get('fault_test_enabled',False) or
            build.get('menu_vm_bus_enabled',False) or
            image[56:60] != b'ARM\x64' or struct.unpack_from('<Q', image, 8)[0] != 0 or
            struct.unpack_from('<Q', image, 16)[0] != build['image_memory_bytes'] or
            build['link_address'] != '0x40000000'):
        raise ValueError('Use the unmodified tablet probe build, not the QEMU test build')
    if kind == 'bare-metal-display-probe':
        version, label = '0.1.0', 'DISPLAY_PROBE'
        if not build.get('strict_alignment'):
            raise ValueError('Rebuild the display probe with strict-alignment code generation')
        if len(image) >= 0x80000: raise ValueError('Display probe exceeds its staging window')
    else:
        version, label = {'pre-kernel-boot-menu':('0.3.2',
            'LINUX_DESKTOP' if (build.get('linux_payload') or {}).get('shares_android_kernel') else
            'LINUX_LIVE' if (build.get('linux_payload') or {}).get('upstream_live_profile') else
            'LINUX_PROBE' if build.get('linux_payload') else 'BOOT_MENU'),
                          'pre-kernel-display-bridge':('0.1.2','DISPLAY_BRIDGE'),
                          'pre-kernel-entry-control':('0.1.1','ENTRY_CONTROL')}[kind]
        if kind in ('pre-kernel-display-bridge','pre-kernel-boot-menu') and not build.get('strict_alignment'):
            raise ValueError('Display bridge requires strict-alignment code generation')
        if build['source_boot_sha256'] != sha(source): raise ValueError('Control uses a different source BOOT')
        original = gzip.decompress(parse(source)['kernel'])
        span = struct.unpack_from('<Q', original, 16)[0]
        if (build['source_kernel_sha256'] != sha(original) or build['observer_offset'] != span or
                len(image) <= span or len(image) >= 0x7c80000 or
                image[span:] != (args.build/'control.bin').read_bytes()):
            raise ValueError('Control does not match its original kernel and compiled observer')
        original_part = bytearray(image[:len(original)])
        original_part[4:8] = original[4:8]; original_part[16:24] = original[16:24]
        if (original_part != original or image[build['banner_offset']:build['banner_offset']+14] != b'Linux version ' or
                image[build['proc_marker_offset']:build['proc_marker_offset']+7] != b'version'):
            raise ValueError('Control premodified the kernel outside its branch/size or baked in its marker')
        if struct.unpack_from('<I', image, 4)[0] != (0x14000000 | ((span-4)//4)):
            raise ValueError('Control entry does not branch to its observer')
    if build.get('version') != version:
        raise ValueError('Build version differs from the package version; rebuild this payload first')
    packed = gzip.compress(image, compresslevel=9, mtime=0)
    if gzip.decompress(packed) != image: raise ValueError('Kernel compression round-trip failed')
    probe, old = replace_kernel(source, packed)
    args.out.mkdir(parents=True, exist_ok=False)
    test = archive(args.out / 'probe', probe, f'AP_KBL_SM-X110_CYE5_{label}_{version}.tar.md5')
    restore = archive(args.out / 'restore', source, f'AP_RESTORE_SM-X110_CYE5_PRE_KBL_BOOT_ONLY_{version}.tar.md5')
    result = {'product':'KBL', 'version': version, 'kind': kind, 'model': 'SM-X110', 'bootloader': 'X110XXU4CYE5',
        'source_boot_sha256': sha(source), 'probe': test, 'restore': restore,
        'image_sha256': sha(image), 'linux_kernel_in_probe': kind != 'bare-metal-display-probe',
        'preserved': {key: sha(old[key]) for key in ('ramdisk', 'signature', 'samsung', 'vbmeta')},
        'boot_header_only_changed_field': 'kernel_size', 'os_version_unchanged': True,
        'embedded_avb_rollback_index_before_after': [0, 0],
        'embedded_avb_rollback_location_before_after': [0, 0],
        'embedded_avb_flags_before_after': [0, 0],
        'writes_only': ['boot'], 'signatures_authenticate_modified_kernel': False,
        'device_boot_verified': False, 'hardware_rollback_counters_verified': False}
    if build.get('linux_payload'):
        result['linux_payload']=build['linux_payload']
        result['uefi_services_provided']=False
        result['linux_hardware_boot_verified']=False
    (args.out / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Prepared one BOOT-only probe and an exact-input BOOT restore:', args.out)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-boot', type=Path, required=True)
    p.add_argument('--build', type=Path, default=ROOT / 'build-probe/gta9wifi')
    p.add_argument('--out', type=Path, required=True)
    package(p.parse_args())
