"""Build a CYE5 pre-kernel execution control or instrumented display bridge."""
import argparse
import gzip
import hashlib
import json
import os
import re
from pathlib import Path
import struct
import subprocess

from build_uefi import ROOT, llvm_dir, run
from package_probe import KNOWN_CYE5_BOOTS, parse

def build(args):
    boot_menu = getattr(args, 'boot_menu', False)
    menu_test = getattr(args, 'menu_test', False)
    reset_fault = getattr(args, 'reset_fault_test', False)
    display_bridge = getattr(args, 'display_bridge', False) or boot_menu
    fault_test = getattr(args, 'fault_test', False)
    linux_image=getattr(args,'linux_image',None)
    linux_initrd=getattr(args,'linux_initrd',None)
    if bool(linux_image)!=bool(linux_initrd) or (linux_image and not boot_menu):
        raise ValueError('Linux requires --boot-menu, --linux-image and --linux-initrd together')
    if menu_test and not boot_menu: raise ValueError('Menu VM bus requires --boot-menu')
    if reset_fault and not (boot_menu and menu_test): raise ValueError('Reset fault injection requires --boot-menu --menu-test')
    if fault_test and not display_bridge: raise ValueError('Fault injection requires --display-bridge')
    raw = args.source_boot.read_bytes()
    source_hash = hashlib.sha256(raw).hexdigest()
    if source_hash not in KNOWN_CYE5_BOOTS:
        raise ValueError('Use an audited original or rooted CYE5 BOOT image')
    original = gzip.decompress(parse(raw)['kernel'])
    if original[:2] != b'MZ' or original[56:60] != b'ARM\x64':
        raise ValueError('Expected the original CYE5 ARM64 EFI-stub kernel')
    original_span = struct.unpack_from('<Q', original, 16)[0]
    if not len(original) <= original_span < 0x7000000 or original_span % 4096:
        raise ValueError('Unexpected kernel memory span')
    instruction = struct.unpack_from('<I', original, 4)[0]
    if instruction >> 26 != 5:
        raise ValueError('Expected the original unconditional ARM64 entry branch')
    delta = instruction & 0x3ffffff
    if delta & (1 << 25): delta -= 1 << 26
    original_entry = 4 + delta * 4
    if not 64 <= original_entry < len(original): raise ValueError('Invalid original kernel entry')
    pattern = b'Linux version 5.10.218-android12-9-30958166 '
    if original.count(pattern) != 1: raise ValueError('Original kernel banner is ambiguous')
    banner = original.index(pattern)
    proc_pattern = b'%s version %s'
    if original.count(proc_pattern) != 1: raise ValueError('Kernel proc-version format is ambiguous')
    proc_marker = original.index(proc_pattern) + 3
    proc_format = proc_marker - 3
    proc_capacity = original.index(b'\0', proc_format) - proc_format + 1
    if display_bridge and proc_capacity < 280: raise ValueError('Proc-version format has insufficient trace space')
    base = 0x40000000
    observer = base + original_span
    linux_metadata=None
    if linux_image:
        payload=linux_image.read_bytes();initrd=linux_initrd.read_bytes()
        if len(payload)<4096 or payload[56:60]!=b'ARM\x64':raise ValueError('Linux payload must be a raw ARM64 Image')
        offset,memory,flags=struct.unpack_from('<3Q',payload,8)
        if flags&1 or offset>=0x200000 or memory<len(payload) or offset+memory>original_span:
            raise ValueError('Linux payload would overwrite the running loader or has an unsupported header')
        if not initrd.startswith((b'070701',b'\x1f\x8b')) or len(initrd)>16*1024*1024:
            raise ValueError('Linux initrd must be a bounded newc or gzip archive')
        linux_metadata={'image_sha256':hashlib.sha256(payload).hexdigest(),'image_file_bytes':len(payload),
            'image_memory_bytes':memory,'text_offset':offset,'load_address':hex(base+offset),
            'initrd_sha256':hashlib.sha256(initrd).hexdigest(),'initrd_bytes':len(initrd)}
        release=re.search(rb'Linux version ([^ \x00]{1,100}) ',payload)
        linux_metadata['kernel_release']=release.group(1).decode('ascii') if release else None
    output = args.out.resolve(); output.mkdir(parents=True, exist_ok=True)
    suffix = '.exe' if os.name == 'nt' else ''
    llvm = llvm_dir(args.llvm_bin)
    clang = llvm / ('clang++' + suffix)
    common = [clang, '--target=aarch64-none-elf', '-O2', '-ffreestanding', '-fno-builtin',
        '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-fno-pic', '-fno-pie',
        '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-mgeneral-regs-only', '-mstrict-align',
        '-fno-vectorize', '-fno-slp-vectorize', '-nostdinc++', '-Wall', '-Wextra', '-Werror',
        '-I'+str(ROOT/'include'), '-I'+str(ROOT/'targets/samsung/gta9wifi')]
    if fault_test: common += ['-DKBL_BRIDGE_FAULT_TEST=1']
    if boot_menu: common += ['-DKBL_BOOT_MENU=1']
    if menu_test: common += ['-DKBL_MENU_VM_TEST=1']
    if reset_fault: common += ['-DKBL_RESET_FAULT_TEST=1']
    if linux_image:common += ['-DKBL_LINUX_PAYLOAD=1','-DKBL_LOADER_START='+hex(observer)]
    config = ['-DKBL_PROC_FORMAT_ADDRESS='+hex(base+proc_format),
              '-DKBL_PROC_FORMAT_CAPACITY='+str(proc_capacity)]
    run(common + ['-DKBL_BANNER_ADDRESS=' + hex(base + banner),
        '-DKBL_PROC_MARKER_ADDRESS=' + hex(base + proc_marker),
        '-DKBL_KERNEL_ENTRY=' + hex(base + original_entry)] + config + ['-c',
        ROOT / ('platforms/baremetal/display_bridge.S' if display_bridge else 'platforms/baremetal/entry_control.S'), '-o', output / 'entry.o'])
    run(common + ['-std=c++17', '-c', ROOT / 'platforms/baremetal/entry_control.cpp', '-o', output / 'control.o'])
    objects = [output/'entry.o', output/'control.o']
    if display_bridge:
        sources = ['platforms/baremetal/display_bridge.cpp','src/core.cpp','src/fdt.cpp','src/menu_view.cpp','src/font.cpp','targets/samsung/gta9wifi/display_probe.cpp']
        if boot_menu:
            sources += ['src/buttons.cpp','targets/samsung/gta9wifi/controls.cpp','platforms/baremetal/boot_menu.cpp']
        if linux_image:sources += ['src/linux_boot.cpp']
        for source in sources:
            obj = output/(Path(source).stem+'.o')
            run(common + config + ['-std=c++17','-c',ROOT/source,'-o',obj]); objects.append(obj)
    if linux_image:
        resources=output/'linux_resources.S'
        resources.write_text('''.section .rodata.linux_payload,"a"
.balign 4096
.global linux_image_begin,linux_image_end,linux_initrd_begin,linux_initrd_end
linux_image_begin:
.incbin "'''+linux_image.resolve().as_posix()+'''"
linux_image_end:
.balign 4096
linux_initrd_begin:
.incbin "'''+linux_initrd.resolve().as_posix()+'''"
linux_initrd_end:
.section .data.linux_dtb,"aw",@progbits
.balign 4096
.global linux_dtb_buffer
linux_dtb_buffer:
.zero 2097152
''')
        run(common+['-c',resources,'-o',output/'linux_resources.o']);objects.append(output/'linux_resources.o')
    script = output / 'control.ld'
    script.write_text(f'''ENTRY(entry_control)
SECTIONS {{
 . = {observer:#x};
 .text : {{ KEEP(*(.text.entry_control)) *(.text .text.*) }}
 .rodata : ALIGN(8) {{ *(.rodata .rodata.*) }}
 .data : ALIGN(16) {{ BYTE(0); *(.data .data.*) *(.bss .bss.* COMMON) }}
 __bridge_file_end = .;
 .stack (NOLOAD) : ALIGN(16) {{ . += {65536 if display_bridge else 0}; __bridge_stack_top = .; }}
 __bridge_memory_end = ALIGN(4096);
 /DISCARD/ : {{ *(.comment .note* .eh_frame* .ARM.exidx* .ARM.extab*) }}
}}
''')
    elf = output / 'control.elf'
    run([llvm / ('ld.lld' + suffix), '-T', script, '--build-id=none', '-o', elf] + objects)
    run([llvm / ('llvm-objcopy' + suffix), '-O', 'binary', elf, output/'control.bin'])
    disassembly = subprocess.check_output([str(llvm/('llvm-objdump'+suffix)), '-d', str(elf)], text=True)
    (output/'control-disassembly.txt').write_text(disassembly)
    # This control intentionally needs no working stack, FDT parser, display,
    # exception vectors, timer or device-register writes.
    if not display_bridge and re.search(r'\b(sp|wsp)\b', disassembly): raise ValueError('Control unexpectedly uses a stack')
    if subprocess.check_output([str(llvm/('llvm-nm'+suffix)), '--undefined-only', str(elf)], text=True).strip():
        raise ValueError('Control has unresolved runtime dependencies')
    code = (output/'control.bin').read_bytes()
    symbol_text = subprocess.check_output([str(llvm/('llvm-nm'+suffix)),'--defined-only','--format=posix',str(elf)],text=True)
    symbols = {p[0]:int(p[2],16) for line in symbol_text.splitlines() if len(p:=line.split())>=3}
    image = bytearray(original)
    image.extend(b'\0' * (original_span - len(image)))
    image.extend(code)
    span = symbols['__bridge_memory_end'] - base
    if not len(image) <= span < 0x7c80000: raise ValueError('Observer exceeds Samsung kernel staging reservation')
    branch_delta = original_span - 4
    if branch_delta % 4 or not 0 <= branch_delta < (1 << 27): raise ValueError('Entry branch is out of range')
    struct.pack_into('<I', image, 4, 0x14000000 | (branch_delta // 4))
    struct.pack_into('<Q', image, 16, span)
    unchanged = bytearray(image[:len(original)])
    unchanged[4:8] = original[4:8]; unchanged[16:24] = original[16:24]
    if unchanged != original or image[banner:banner+14] != b'Linux version ':
        raise ValueError('Original kernel changed outside its entry branch/size')
    (output/'Image').write_bytes(image)
    manifest = {'product':'KBL', 'version':'0.2.0' if boot_menu else '0.1.2' if display_bridge else '0.1.1',
        'kind':'pre-kernel-boot-menu' if boot_menu else 'pre-kernel-display-bridge' if display_bridge else 'pre-kernel-entry-control', 'target':'SM-X110-CYE5',
        'qemu_uart_enabled':False, 'link_address':hex(base), 'image_file_bytes':len(image),
        'image_memory_bytes':span, 'image_sha256':hashlib.sha256(image).hexdigest(),
        'source_boot_sha256':source_hash, 'source_kernel_sha256':hashlib.sha256(original).hexdigest(),
        'source_kernel_file_bytes':len(original), 'source_kernel_memory_bytes':original_span,
        'original_entry_offset':original_entry, 'observer_offset':original_span,
        'observer_bytes':len(code), 'banner_offset':banner, 'runtime_marker':'KBL bootstrap ',
        'proc_marker_offset':proc_marker, 'proc_runtime_marker':'KBL_CPP',
        'proc_format_offset':proc_format, 'proc_format_capacity':proc_capacity,
        'banner_prepatched':False, 'uses_stack':display_bridge, 'device_boot_verified':False,
        'strict_alignment':True, 'fault_test_enabled':fault_test or reset_fault,
        'reset_fault_test_enabled':reset_fault, 'menu_vm_bus_enabled':menu_test,
        'menu_enabled':boot_menu, 'symbols':symbols}
    if boot_menu:
        manifest['linux_payload']=linux_metadata
        manifest['screen_font']=json.loads((ROOT/'assets/fonts/jetbrains-mono/baked.json').read_text())
        manifest['reset_interface']='psci-system-reset-smc'
        manifest['reset_function']='0x84000009'
        manifest['direct_watchdog_reset']=False
    (output/'build.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('Built '+('boot menu' if boot_menu else 'display bridge' if display_bridge else 'stackless C++ entry control')+'; original kernel entry/banner retained:', output/'Image')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-boot',type=Path,required=True)
    parser.add_argument('--llvm-bin')
    parser.add_argument('--display-bridge',action='store_true')
    parser.add_argument('--boot-menu',action='store_true',help='Build the experimental SM-X110 hardware boot menu')
    parser.add_argument('--linux-image',type=Path,help='Optional independent raw ARM64 Linux Image')
    parser.add_argument('--linux-initrd',type=Path,help='Initramfs for the independent Linux entry')
    parser.add_argument('--menu-test',action='store_true',help='VM-only input bus; tablet packaging rejects this build')
    parser.add_argument('--reset-fault-test',action='store_true',help='VM-only exception during reset; requires --boot-menu --menu-test')
    parser.add_argument('--fault-test',action='store_true',help='VM-only fault injection; tablet packaging rejects this build')
    parser.add_argument('--out',type=Path)
    args=parser.parse_args()
    if args.out is None:
        args.out=ROOT/('build-boot-menu' if args.boot_menu else 'build-display-bridge' if args.display_bridge else 'build-entry-control')/('reset-fault-test' if args.reset_fault_test else 'menu-test' if args.menu_test else 'fault-test' if args.fault_test else 'gta9wifi')
    build(args)
