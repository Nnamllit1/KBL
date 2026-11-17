"""Boot the ARM64 menu, recover from a bad entry, chainload EFI, then boot Linux."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
KERNEL_URL = 'https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/netboot/vmlinuz-virt'

class QMP:
    def __init__(self, port, deadline):
        while True:
            try:
                self.socket = socket.create_connection(('127.0.0.1', port), timeout=2)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.2)
        self.socket.settimeout(10)
        self.stream = self.socket.makefile('rwb')
        json.loads(self.stream.readline())
        self.command('qmp_capabilities')

    def command(self, name, arguments=None):
        request = {'execute': name}
        if arguments is not None:
            request['arguments'] = arguments
        self.stream.write(json.dumps(request).encode() + b'\n')
        self.stream.flush()
        while True:
            response = json.loads(self.stream.readline())
            if 'error' in response:
                raise RuntimeError(response)
            if 'return' in response:
                return response['return']

    def key(self, key):
        self.command('send-key', {'keys': [{'type': 'qcode', 'data': key}]})
        time.sleep(.25)

    def close(self):
        self.stream.close()
        self.socket.close()

def archive(files):
    output = bytearray()
    for inode, (name, data, mode) in enumerate(files + [('TRAILER!!!', b'', 0)], 1):
        console = name == 'dev/console'
        name = name.encode() + b'\0'
        fields = [inode, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name), 0]
        if console:
            fields[9:11] = [5, 1]
        output += b'070701' + b''.join(f'{v:08x}'.encode() for v in fields)
        output += name
        output += b'\0' * (-len(output) % 4)
        output += data
        output += b'\0' * (-len(output) % 4)
    return output

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', required=True, type=Path)
    parser.add_argument('--firmware', required=True, type=Path)
    parser.add_argument('--build', type=Path, default=ROOT / 'build-uefi/arm64-test')
    parser.add_argument('--out', type=Path, default=ROOT / 'build-vm')
    parser.add_argument('--kernel', type=Path, help='Use an existing Alpine ARM64 virt kernel instead of downloading')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    esp = out / 'esp'
    (esp / 'EFI/BOOT').mkdir(parents=True, exist_ok=True)
    if args.kernel:
        kernel = args.kernel.read_bytes()
    else:
        cache = ROOT / '.cache/alpine-v3.22-aarch64-vmlinuz-virt'
        cache.parent.mkdir(exist_ok=True)
        if not cache.exists():
            print('Downloading ARM64 test kernel from Alpine Linux...', flush=True)
            with urllib.request.urlopen(KERNEL_URL, timeout=60) as response:
                data = response.read(64 * 1024 * 1024 + 1)
            if len(data) > 64 * 1024 * 1024:
                raise RuntimeError('kernel download exceeds limit')
            cache.write_bytes(data)
        kernel = cache.read_bytes()
    downloaded_sha = hashlib.sha256(kernel).hexdigest()
    if kernel.startswith(b'\x1f\x8b'):
        kernel = gzip.decompress(kernel)
    pe = struct.unpack_from('<I', kernel, 60)[0] if len(kernel) >= 64 else len(kernel)
    if (kernel[:2] != b'MZ' or pe + 6 > len(kernel) or kernel[pe:pe+4] != b'PE\0\0'
            or kernel[pe+4:pe+6] != b'\x64\xaa'):
        raise RuntimeError('test kernel is not an ARM64 EFI executable')
    build = args.build.resolve()
    shutil.copyfile(build / 'BOOTAA64.EFI', esp / 'EFI/BOOT/BOOTAA64.EFI')
    shutil.copyfile(build / 'test-payload.efi', esp / 'test-payload.efi')
    (esp / 'Linux.efi').write_bytes(kernel)
    (esp / 'initramfs.cpio').write_bytes(archive([
        ('init', (build / 'linux-init').read_bytes(), 0o100755),
        ('dev', b'', 0o040755),
        ('dev/console', b'', 0o020600),
    ]))
    (esp / 'kbl.cfg').write_text(
        'default=missing\ntimeout=0\n'
        'entry=missing|Missing image (failure test)|efi|/missing.efi||\n'
        'entry=payload|EFI chainload test|efi|/test-payload.efi||\n'
        'entry=linux|ARM64 Linux|efi|/Linux.efi|console=ttyAMA0 rdinit=/init panic=-1|/initramfs.cpio\n'
        'entry=off|Power off|poweroff|||\n', encoding='ascii')
    # Bind an ephemeral local port; QEMU receives a fresh VM name for this run.
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    log = out / 'serial.log'
    stderr = out / 'qemu-stderr.log'
    descriptor = {
        'kernel_url': KERNEL_URL if not args.kernel else None,
        'kernel_download_sha256': downloaded_sha,
        'kernel_image_sha256': hashlib.sha256(kernel).hexdigest(),
        'firmware_sha256': hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
        'menu_sha256': hashlib.sha256((build / 'BOOTAA64.EFI').read_bytes()).hexdigest(),
        'checks': {},
    }
    command = [str(args.qemu.resolve()), '-name', 'kbl-arm64-smoke-' + str(time.time_ns()),
        '-machine', 'virt', '-cpu', 'cortex-a76', '-accel', 'tcg', '-m', '1024', '-smp', '2',
        '-bios', str(args.firmware.resolve()), '-display', 'none', '-monitor', 'none',
        '-serial', 'file:' + str(log), '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off',
        '-device', 'virtio-gpu-pci', '-device', 'qemu-xhci', '-device', 'usb-kbd',
        '-drive', 'if=none,id=esp,format=raw,file=fat:rw:' + str(esp),
        '-device', 'virtio-blk-pci,drive=esp', '-no-reboot']
    process = None
    qmp = None
    start = time.monotonic()
    deadline = start + 240
    def wait_for(marker, offset=0):
        while time.monotonic() < deadline:
            content = log.read_bytes() if log.exists() else b''
            position = content.find(marker.encode(), offset)
            if position >= 0:
                return position + len(marker)
            if process.poll() is not None:
                raise RuntimeError(f'QEMU exited ({process.returncode}) before {marker}; see {log}')
            time.sleep(.2)
        raise TimeoutError(f'No {marker}; see {log}')
    try:
        print('Starting headless ARM64 UEFI VM...', flush=True)
        with stderr.open('wb') as errors:
            process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=errors,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            qmp = QMP(port, deadline)
            checkpoint = wait_for('KBL_MENU_READY')
            descriptor['checks']['menu'] = True
            qmp.command('screendump', {'filename': str(out / 'menu.png'), 'format': 'png'})
            print('Menu running; checking failed-image return...', flush=True)
            qmp.key('ret')
            checkpoint = wait_for('Boot failed.', checkpoint)
            descriptor['checks']['failed_image_returns_to_menu'] = True
            qmp.key('down'); qmp.key('ret')
            checkpoint = wait_for('KBL_CHAINLOAD_OK', checkpoint)
            wait_for('KBL_IMAGE_RETURNED', checkpoint)
            descriptor['checks']['efi_chainload_and_return'] = True
            print('EFI chainload passed; booting ARM64 Linux with initrd...', flush=True)
            qmp.key('down'); qmp.key('ret')
            wait_for('KBL_INITRD_DELIVERED', checkpoint)
            wait_for('KBL_LINUX_INIT_OK', checkpoint)
            descriptor['checks']['linux_initrd_userspace'] = True
            process.wait(timeout=20)
            descriptor['checks']['linux_poweroff'] = process.returncode == 0
            if process.returncode:
                raise RuntimeError(f'QEMU returned {process.returncode}')
            print('PASS: menu, failure recovery, EFI chainload, Linux initrd/userspace, power off', flush=True)
    finally:
        if process and process.poll() is None:
            if qmp:
                try: qmp.command('quit')
                except (OSError, ValueError, RuntimeError): pass
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate(); process.wait(timeout=5)
        if qmp:
            qmp.close()
        descriptor['elapsed_seconds'] = round(time.monotonic() - start, 2)
        (out / 'result.json').write_text(json.dumps(descriptor, indent=2) + '\n')

if __name__ == '__main__':
    main()
