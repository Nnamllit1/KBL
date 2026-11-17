"""Boot a real independent Linux kernel through the pre-kernel KBL menu in QEMU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import time

from build_uefi import ROOT,llvm_dir,run
from fdt_fixture import decode,encode,graft_controls
from probe_smoke import BASE,FRAME,png
from vm_smoke import QMP


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qemu',required=True,type=Path);p.add_argument('--build',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path)
    args=p.parse_args();build=args.build.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    meta=json.loads((build/'build.json').read_text());image=(build/'Image').read_bytes()
    assert meta['linux_payload'] and meta['menu_vm_bus_enabled'] and hashlib.sha256(image).hexdigest()==meta['image_sha256']
    llvm=llvm_dir();suffix='.exe' if os.name=='nt' else '';results=[]
    for el in (1,2):
        case=out/f'el{el}';case.mkdir(exist_ok=True)
        machine='virt,gic-version=3,virtualization='+('on' if el==2 else 'off')
        run([args.qemu,'-machine',machine+',dumpdtb='+str(case/'qemu.dtb'),'-cpu','cortex-a76','-m','1024',
             '-smp','1','-display','none','-serial','none','-monitor','none'])
        root,reservations,boot=decode((case/'qemu.dtb').read_bytes());graft_controls(root)
        reservations.append((0x52000000,4096))
        (case/'live.dtb').write_bytes(encode(root,reservations,boot))
        events=[(300,2),(500,0),(700,4),(900,0)]
        (case/'menu-bus.bin').write_bytes(struct.pack('<4I',len(events),0,0,0)+
            b''.join(struct.pack('<II',*e) for e in events))
        (case/'loader.S').write_text(f'''.text
.global _start
_start:
 ldr x1,=0x60000000
 ldr x2,=0x40000000
 ldr x3,={len(image)}
1: ldrb w6,[x1],#1
 strb w6,[x2],#1
 subs x3,x3,#1
 b.ne 1b
 dsb sy
 ic iallu
 dsb sy
 isb
 msr SPSel,#1
 ldr x10,=0x4e001000
 mov sp,x10
 ldr x10,=0x4e100000
 msr {'vbar_el1' if el==1 else 'vbar_el2'},x10
 mrs x10,{'sctlr_el1' if el==1 else 'sctlr_el2'}
 orr x10,x10,#2
 msr {'sctlr_el1' if el==1 else 'sctlr_el2'},x10
 isb
 ldr x0,=0x48000000
 mov x1,xzr
 mov x2,xzr
 mov x3,xzr
 ldr x9,=0x40000000
 br x9
 .ltorg
''')
        run([llvm/('clang'+suffix),'--target=aarch64-none-elf','-c',case/'loader.S','-o',case/'loader.o'])
        run([llvm/('ld.lld'+suffix),'-Ttext=0x4c000000','--entry=_start','-o',case/'loader.elf',case/'loader.o'])
        run([llvm/('llvm-objcopy'+suffix),'-O','binary',case/'loader.elf',case/'loader.bin'])
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        log=case/'serial.log';q=None;process=None
        command=[str(args.qemu.resolve()),'-name',f'kbl-linux-el{el}-{time.time_ns()}',
            '-machine',machine,'-cpu','cortex-a76','-m','1024','-smp','1',
            '-device',f'loader,file={build/"Image"},addr=0x60000000,force-raw=on',
            '-device',f'loader,file={case/"live.dtb"},addr=0x48000000,force-raw=on',
            '-device',f'loader,file={case/"menu-bus.bin"},addr=0x52000000,force-raw=on',
            '-device',f'loader,file={case/"loader.bin"},addr=0x4c000000,cpu-num=0,force-raw=on',
            '-display','none','-monitor','none','-serial','file:'+str(log),
            '-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off','-no-reboot']
        try:
            with (case/'stderr.log').open('wb') as errors:
                process=subprocess.Popen(command,stdout=subprocess.DEVNULL,stderr=errors,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                q=QMP(port,time.monotonic()+10);deadline=time.monotonic()+60
                while time.monotonic()<deadline:
                    content=log.read_bytes() if log.exists() else b''
                    marker=b'KBL_UPSTREAM_LIVE_INIT_OK' if meta['linux_payload'].get('upstream_live_profile') else b'KBL_LINUX_INIT_OK'
                    if marker in content and (not meta['linux_payload'].get('upstream_live_profile') or b'KBL_UPSTREAM_LIVE_ALIVE' in content):break
                    if process.poll() is not None:raise RuntimeError('Linux VM exited before PID 1; see '+str(log))
                    time.sleep(.2)
                else:
                    q.command('stop')
                    (case/'failed-registers.txt').write_text(q.command('human-monitor-command',{'command-line':'info registers'}))
                    q.command('pmemsave',{'val':meta['symbols']['bridge_context'],'size':280,'filename':str(case/'context.bin')})
                    raise RuntimeError('No independent Linux PID 1; see '+str(log))
                release=meta['linux_payload'].get('kernel_release')
                assert (b'Linux version ' in content or (release and release.encode() in content))
                assert b'Kernel panic' not in content
                q.command('stop')
                q.command('pmemsave',{'val':BASE-16,'size':FRAME*3+32,'filename':str(case/'framebuffer.bin')})
                pixels=(case/'framebuffer.bin').read_bytes();assert not any(pixels[:16]+pixels[-16:])
                planes=pixels[16:-16]
                if meta['linux_payload'].get('upstream_live_profile'):
                    assert b'[drm] Initialized simpledrm' in content,'Upstream simpledrm did not initialize'
                    assert b'fb0: simpledrmdrmfb frame buffer device' in content,'Framebuffer console did not bind'
                    assert b'KBL_UPSTREAM_LIVE_ALIVE' in content
                else:assert planes[:FRAME]==planes[FRAME:2*FRAME]==planes[2*FRAME:]
                png(case/'screen.png',planes[:800*1340*4])
                q.command('pmemsave',{'val':0x48000000,'size':len((case/'live.dtb').read_bytes()),'filename':str(case/'unchanged-live.dtb')})
                assert (case/'unchanged-live.dtb').read_bytes()==(case/'live.dtb').read_bytes(),'Live Android DT was changed'
                results.append({'el':el,'real_linux_kernel':True,'independent_initramfs_pid1':True,
                    'original_live_dtb_unchanged':True,'framebuffer_guards_intact':True,
                    'image_sha256':meta['linux_payload']['image_sha256'],'hardware_verified':False,
                    'upstream_live_profile':bool(meta['linux_payload'].get('upstream_live_profile'))})
                print(f'PASS KBL -> real Linux -> independent PID 1, EL{el}',flush=True)
        finally:
            if process and process.poll() is None:
                if q:
                    try:q.command('quit')
                    except (OSError,ValueError,RuntimeError):pass
                try:process.wait(timeout=3)
                except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=3)
            if q:q.close()
    (out/'result.json').write_text(json.dumps({'kbl_image_sha256':meta['image_sha256'],'tests':results},indent=2)+'\n')


if __name__=='__main__':main()
