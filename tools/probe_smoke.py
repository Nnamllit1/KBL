"""Execute the bare-metal probe at EL1/EL2 and inspect its framebuffer writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import time
import zlib

from build_uefi import ROOT
from vm_smoke import QMP

BASE = 0x50000000
FRAME = 800 * 1344 * 4
SIZE = 0xc50000

def test_dtb(controls=False, psci_method=b'smc\0'):
    strings = bytearray()
    names = {}
    def be(n): return struct.pack('>I', n)
    def prop(name, data):
        if name not in names:
            names[name] = len(strings); strings.extend(name.encode() + b'\0')
        return be(3) + be(len(data)) + be(names[name]) + data + b'\0' * (-len(data) % 4)
    def node(name, properties, children=()):
        name = name.encode() + b'\0'
        return be(1) + name + b'\0' * (-len(name) % 4) + b''.join(properties) + b''.join(children) + be(2)
    chosen = node('chosen', [
        # DT properties are only 4-byte aligned. Match the real tablet's
        # framebuffer tag layout rather than accidentally granting 8-byte alignment.
        prop('kbl-test-alignment-pad', b''),
        prop('atag,videolfb', struct.pack('<QIII', BASE, 1, 6000, SIZE)),
        prop('atag,videolfb-fb_base_l', be(BASE)), prop('atag,videolfb-fb_base_h', be(0)),
        prop('atag,videolfb-vramSize', be(SIZE))])
    memory = node('memory@40000000', [prop('device_type', b'memory\0'), prop('reg', struct.pack('>QQ', 0x40000000, 0x40000000))])
    children = [chosen, memory]
    if controls:
        registers = lambda base, size: struct.pack('>QQ', base, size)
        keymap = [0, 115] + [0] * 70
        keypad = node('kp@10010000', [prop('compatible', b'mediatek,kp\0'),
            prop('reg', registers(0x10010000, 0x1000)), prop('mediatek,hw-map-num', be(72)),
            prop('mediatek,hw-init-map', b''.join(be(k) for k in keymap))])
        keys = node('mt6366keys', [prop('compatible', b'mediatek,mt6366-keys\0')], [
            node('power', [prop('linux,keycodes', be(116))]), node('home', [prop('linux,keycodes', be(114))])])
        pwrap = node('pwrap@10026000', [prop('compatible', b'mediatek,mt6789-pwrap\0'),
            prop('reg', registers(0x10026000, 0x1000))], [node('mt6366', [], [keys])])
        mode = node('reboot-mode', [prop('compatible', b'syscon-reboot-mode\0'), prop('offset', be(0x24)),
            prop('mask', be(15)), prop('mode-recovery', be(2)), prop('mode-bootloader', be(3))])
        watchdog = node('watchdog@10007000', [prop('compatible', b'mediatek,mt6789-wdt\0'),
            prop('reg', registers(0x10007000, 0x100))], [mode])
        children += [node('soc', [], [keypad, pwrap, watchdog]),
            node('psci', [prop('compatible',b'arm,psci-1.0\0'),prop('method',psci_method)])]
    root = node('', [prop('model', b'MT8781V/NA\0'), prop('#address-cells', be(2)), prop('#size-cells', be(2))], children)
    structure = root + be(9)
    total = 56 + len(structure) + len(strings)
    header = struct.pack('>10I', 0xd00dfeed, total, 56, 56 + len(structure), 40, 17, 16, 0, len(strings), len(structure))
    return header + b'\0' * 16 + structure + strings

def png(path, bgrx):
    rgb = bytearray(800 * 1340 * 3)
    rgb[0::3] = bgrx[2::4]; rgb[1::3] = bgrx[1::4]; rgb[2::3] = bgrx[0::4]
    rows = b''.join(b'\0' + rgb[y*2400:(y+1)*2400] for y in range(1340))
    def chunk(name, data):
        return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name + data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 800, 1340, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qemu', required=True, type=Path)
    p.add_argument('--build', type=Path, default=ROOT / 'build-probe/qemu')
    p.add_argument('--out', type=Path, default=ROOT / 'build-probe/vm-test')
    args = p.parse_args()
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    build = args.build.resolve()
    manifest = json.loads((build / 'build.json').read_text())
    if not manifest['qemu_uart_enabled']:
        raise ValueError('Use build_probe.py --qemu for this test')
    image = (build / 'Image').read_bytes()
    if hashlib.sha256(image).hexdigest() != manifest['image_sha256']:
        raise ValueError('Image does not match build manifest')
    (out / 'test.dtb').write_bytes(test_dtb())
    results = []
    for level, offset in [(1, 0x80000), (2, 0x80000)]:
        test = out / ('el' + str(level)); test.mkdir(exist_ok=True)
        loaded = bytearray(image); struct.pack_into('<Q', loaded, 8, offset)
        (test / 'Image').write_bytes(loaded)
        log = test / 'serial.log'
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0)); port = reservation.getsockname()[1]
        command = [str(args.qemu.resolve()), '-name', f'kbl-baremetal-el{level}-{time.time_ns()}',
            '-machine', 'virt,gic-version=3,virtualization=' + ('on' if level == 2 else 'off'),
            '-cpu', 'cortex-a76', '-m', '1024', '-smp', '1', '-accel', 'tcg',
            '-kernel', str(test / 'Image'), '-dtb', str(out / 'test.dtb'),
            '-display', 'none', '-monitor', 'none', '-serial', 'file:' + str(log),
            '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off', '-no-reboot']
        qmp = None; process = None
        deadline = time.monotonic() + 40
        try:
            with (test / 'stderr.log').open('wb') as errors:
                process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=errors,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
                qmp = QMP(port, deadline)
                diagnostic = False
                while time.monotonic() < deadline:
                    content = log.read_text(errors='replace') if log.exists() else ''
                    if 'KBL_BARE_METAL_HEARTBEAT' in content: break
                    if process.poll() is not None: raise RuntimeError('QEMU exited; see ' + str(test))
                    if not diagnostic and time.monotonic() > deadline - 35:
                        (test/'early-registers.txt').write_text(qmp.command('human-monitor-command', {'command-line': 'info registers'}))
                        diagnostic = True
                    time.sleep(.1)
                else: raise TimeoutError('No bare-metal heartbeat; see ' + str(test))
                if f'KBL_BARE_METAL_EL{level}' not in content: raise RuntimeError('Unexpected CPU exception level')
                dump = test / 'framebuffer.bin'
                qmp.command('stop')
                qmp.command('pmemsave', {'val': BASE - 16, 'size': FRAME * 3 + 32, 'filename': str(dump)})
                pixels = dump.read_bytes()
                if any(pixels[:16]) or any(pixels[-16:]): raise RuntimeError('Framebuffer bounds overwritten')
                planes = pixels[16:-16]
                if planes[:FRAME] != planes[FRAME:2*FRAME] or planes[:FRAME] != planes[2*FRAME:]:
                    raise RuntimeError('Framebuffer copies differ')
                def pixel(x, y): return struct.unpack_from('<I', planes, (y*800+x)*4)[0]
                if pixel(0, 0) != 0xff00c7aa or pixel(100, 860) != 0xffff4040:
                    raise RuntimeError('Framebuffer pattern is incorrect')
                png(test / 'screen.png', planes[:800*1340*4])
                registers = qmp.command('human-monitor-command', {'command-line': 'info registers'})
                (test / 'registers.txt').write_text(registers)
                pc = re.search(r'PC=([0-9a-fA-F]+)', registers)
                if not pc or not (0x40000000 <= int(pc[1], 16) < 0x40000000 + len(image)):
                    raise RuntimeError('Probe did not execute at its linked address')
                results.append({'exception_level': level, 'load_text_offset': offset,
                    'entry_draw_heartbeat': True, 'bounds_and_buffer_copies': True,
                    'relocated_pc': pc[1], 'screen_sha256': hashlib.sha256((test/'screen.png').read_bytes()).hexdigest()})
                print(f'PASS: bare-metal EL{level}, offset {offset:#x}, display and heartbeat', flush=True)
        finally:
            if process and process.poll() is None:
                if qmp:
                    try: qmp.command('quit')
                    except (OSError, ValueError, RuntimeError): pass
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.terminate(); process.wait(timeout=5)
            if qmp: qmp.close()
    (out / 'result.json').write_text(json.dumps({'image_sha256': manifest['image_sha256'], 'tests': results}, indent=2) + '\n')

if __name__ == '__main__': main()
