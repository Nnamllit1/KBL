"""Build the freestanding SM-X110 display probe (no flashing or device access)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

from build_uefi import ROOT, llvm_dir, run

def build(llvm, output, qemu=False):
    output.mkdir(parents=True, exist_ok=True)
    ext = '.exe' if os.name == 'nt' else ''
    clang = llvm / ('clang++' + ext)
    ld = llvm / ('ld.lld' + ext)
    objcopy = llvm / ('llvm-objcopy' + ext)
    nm = llvm / ('llvm-nm' + ext)
    common = [clang, '--target=aarch64-none-elf', '-O2', '-ffreestanding', '-fno-builtin',
              '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-fno-pic', '-fno-pie',
              '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-mgeneral-regs-only', '-mstrict-align',
              '-fno-vectorize', '-fno-slp-vectorize', '-nostdinc++', '-Wall', '-Wextra', '-Werror',
              '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'targets/samsung/gta9wifi')]
    if qemu:
        common += ['-DKBL_PROBE_QEMU=1']
    sources = ['platforms/baremetal/entry.S', 'platforms/baremetal/main.cpp', 'src/core.cpp',
               'src/fdt.cpp','src/menu_view.cpp','src/font.cpp', 'targets/samsung/gta9wifi/display_probe.cpp']
    objects = []
    for source in sources:
        obj = output / (Path(source).stem + '.o')
        run(common + ([] if source.endswith('.S') else ['-std=c++17']) + ['-c', ROOT / source, '-o', obj])
        objects.append(obj)
    elf = output / 'probe.elf'
    run([ld, '-T', ROOT / 'platforms/baremetal/probe.ld', '--build-id=none', '--gc-sections',
         '-Map=' + str(output / 'probe.map'), '-o', elf] + objects)
    run([objcopy, '-O', 'binary', elf, output / 'Image'])
    symbols = subprocess.check_output([str(nm), '--defined-only', '--format=posix', str(elf)], text=True)
    addresses = {parts[0]: int(parts[2], 16) for line in symbols.splitlines() if len(parts := line.split()) >= 3}
    image = bytearray((output / 'Image').read_bytes())
    assert len(image) == addresses['__file_size']
    span = addresses['__image_size'] - addresses['_image_start']
    assert 0 < span <= 0x80000 and image[56:60] == b'ARM\x64'
    struct.pack_into('<Q', image, 16, span)
    (output / 'Image').write_bytes(image)
    manifest = {'version': '0.1.0', 'target': 'SM-X110-CYE5', 'kind': 'bare-metal-display-probe',
                'qemu_uart_enabled': qemu, 'linux_kernel': False, 'uefi_services_required': False,
                'link_address': hex(addresses['_image_start']), 'image_file_bytes': len(image),
                'image_memory_bytes': span, 'image_sha256': hashlib.sha256(image).hexdigest(),
                'strict_alignment': True, 'device_boot_verified': False, 'symbols': addresses}
    (output / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Built bare-metal display probe:', output / 'Image', f'({len(image)} bytes)')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--llvm-bin')
    parser.add_argument('--qemu', action='store_true', help='Add PL011 tracing for the VM test; not for tablet packaging')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    build(llvm_dir(args.llvm_bin), (args.out or ROOT / 'build-probe' / ('qemu' if args.qemu else 'gta9wifi')).resolve(), args.qemu)
