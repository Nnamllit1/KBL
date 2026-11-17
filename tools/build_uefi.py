"""Cross-compile the freestanding C++ adapter with LLVM; no Linux VM required."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def llvm_dir(explicit=None):
    if explicit:
        return Path(explicit).resolve()
    if os.environ.get('LLVM_BIN'):
        return Path(os.environ['LLVM_BIN'])
    if shutil.which('clang++') and shutil.which('ld.lld'):
        return Path(shutil.which('clang++')).parent
    sdk = Path(os.environ.get('ANDROID_HOME', Path.home() / 'AppData/Local/Android/Sdk'))
    roots = sorted((sdk / 'ndk').glob('*/toolchains/llvm/prebuilt/*/bin'), reverse=True)
    if roots:
        return roots[0]
    raise SystemExit('Set LLVM_BIN or pass --llvm-bin to a directory containing clang++ and ld.lld')

def run(args):
    p = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(' '.join(map(str, args)) + '\n' + p.stdout + p.stderr)

def build(llvm, arch, output, trace=False):
    suffix = '.exe' if os.name == 'nt' else ''
    clang = llvm / ('clang++' + suffix)
    linker = llvm / ('ld.lld' + suffix)
    output.mkdir(parents=True, exist_ok=True)
    target, machine, filename = {
        'arm64': ('aarch64-pc-windows-msvc', 'arm64', 'BOOTAA64.EFI'),
        'x64': ('x86_64-pc-windows-msvc', 'x64', 'BOOTX64.EFI'),
    }[arch]
    common = [clang, '--target=' + target, '-std=c++17', '-O2', '-ffreestanding',
              '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-mno-stack-arg-probe',
              '-fno-builtin', '-nostdinc++', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'include')]
    if trace:
        common.append('-DKBL_TRACE=1')
    def efi_binary(sources, destination):
        objects = []
        for source in sources:
            obj = output / (Path(source).stem + '.obj')
            run(common + ['-c', ROOT / source, '-o', obj])
            objects.append(obj)
        run([linker, '-flavor', 'link', '/subsystem:efi_application', '/entry:efi_main',
             '/nodefaultlib', '/machine:' + machine, '/out:' + str(destination), '/opt:ref',
             '/dynamicbase', '/fixed:no'] + objects)
    efi_binary(['src/core.cpp', 'src/boot_image.cpp', 'src/fdt.cpp', 'platforms/uefi/main.cpp'], output / filename)
    efi_binary(['tests/efi_payload.cpp'], output / 'test-payload.efi')
    if arch == 'arm64':
        run([clang, '--target=aarch64-linux-gnu', '-std=c++17', '-O2', '-ffreestanding',
             '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-nostdlib', '-static',
             '-fno-pie', '-fuse-ld=lld', '-Wl,-e,_start', ROOT / 'tests/linux_init.cpp',
             '-o', output / 'linux-init'])
    print('Built', output / filename)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--llvm-bin')
    parser.add_argument('--arch', choices=['arm64', 'x64'], default='arm64')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--trace', action='store_true', help='Enable VM test markers in the firmware console')
    args = parser.parse_args()
    build(llvm_dir(args.llvm_bin), args.arch,
          (args.out or ROOT / 'build-uefi' / (args.arch + ('-test' if args.trace else ''))).resolve(), args.trace)

