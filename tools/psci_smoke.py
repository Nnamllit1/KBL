"""Exercise the compiled SMC wrapper against a VM-only EL3 monitor, without reset."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import time

from build_uefi import ROOT, llvm_dir, run
from vm_smoke import QMP


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qemu',required=True,type=Path)
    p.add_argument('--build',type=Path,default=ROOT/'build-boot-menu/gta9wifi')
    p.add_argument('--out',type=Path,default=ROOT/'build-boot-menu/psci-test')
    args=p.parse_args();build=args.build.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    meta=json.loads((build/'build.json').read_text())
    assert meta['reset_interface']=='psci-system-reset-smc'
    assert hashlib.sha256((build/'Image').read_bytes()).hexdigest()==meta['image_sha256']
    llvm=llvm_dir();suffix='.exe' if os.name=='nt' else '';results=[]
    for el in (1,2):
        case=out/f'el{el}';case.mkdir(exist_ok=True)
        source=f'''.text
.global _start
_start:
    ldr x10,=0x4e100000
    mov sp,x10
    ldr x10,=vectors
    msr vbar_el3,x10
    mov x10,#0x501
    msr scr_el3,x10
    mov x10,#0x80000000
    msr hcr_el2,x10
    ldr x10,=0x30d00800
    msr sctlr_el1,x10
    mov x10,#0
    msr sctlr_el2,x10
    ldr x10,=caller
    msr elr_el3,x10
    mov x10,#{0x3c5 if el==1 else 0x3c9}
    msr spsr_el3,x10
    isb
    eret
caller:
    ldr x10,=0x4e001000
    mov sp,x10
    mov x18,#0x1818
    mov x19,#0x1919
    mov x29,#0x2929
    ldr w0,=0x84000009
    ldr x9,={meta['symbols']['bridge_psci_system_reset']}
    blr x9
    ldr x10,=0x51000100
    str x0,[x10,#8]
    str x18,[x10,#16]
    str x19,[x10,#24]
    str x29,[x10,#32]
    mov x11,sp
    str x11,[x10,#40]
    mrs x11,CurrentEL
    str x11,[x10,#48]
    mov x11,#1
    str x11,[x10]
1:  b 1b
    .ltorg
.balign 2048
vectors:
.rept 16
    b monitor
    .space 124
.endr
monitor:
    // A test monitor records the real SMC instruction's inputs, then rejects
    // the request. It never implements SYSTEM_RESET or touches device MMIO.
    ldr x18,=0x51000000
    stp x0,x1,[x18]
    stp x2,x3,[x18,#16]
    stp x4,x5,[x18,#32]
    stp x6,x7,[x18,#48]
    stp x8,x9,[x18,#64]
    stp x10,x11,[x18,#80]
    stp x12,x13,[x18,#96]
    stp x14,x15,[x18,#112]
    stp x16,x17,[x18,#128]
    mrs x0,esr_el3
    str x0,[x18,#144]
    mov w0,#0xffffffff
    mov x18,#0xbeef
    eret
    .ltorg
'''
        (case/'monitor.S').write_text(source)
        run([llvm/('clang'+suffix),'--target=aarch64-none-elf','-c',case/'monitor.S','-o',case/'monitor.o'])
        run([llvm/('ld.lld'+suffix),'-Ttext=0x4c000000','--entry=_start','-o',case/'monitor.elf',case/'monitor.o'])
        run([llvm/('llvm-objcopy'+suffix),'-O','binary',case/'monitor.elf',case/'monitor.bin'])
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        command=[str(args.qemu.resolve()),'-name',f'kbl-psci-call-el{el}',
            '-machine','virt,secure=on,virtualization=on,gic-version=3','-cpu','cortex-a76','-m','1024','-smp','1',
            '-device',f'loader,file={build/"control.bin"},addr={hex(0x40000000+meta["observer_offset"])},force-raw=on',
            '-device',f'loader,file={case/"monitor.bin"},addr=0x4c000000,cpu-num=0,force-raw=on',
            '-display','none','-serial','none','-monitor','none','-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off']
        process=None;q=None
        try:
            with (case/'stderr.log').open('wb') as errors:
                process=subprocess.Popen(command,stdout=subprocess.DEVNULL,stderr=errors,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                q=QMP(port,time.monotonic()+10);deadline=time.monotonic()+10
                while time.monotonic()<deadline:
                    time.sleep(.1);q.command('stop')
                    q.command('pmemsave',{'val':0x51000000,'size':312,'filename':str(case/'state.bin')})
                    raw=(case/'state.bin').read_bytes();state=struct.unpack_from('<7Q',raw,256)
                    if state[0]:break
                    q.command('cont')
                else:
                    (case/'registers.txt').write_text(q.command('human-monitor-command',{'command-line':'info registers'}))
                    raise RuntimeError('SMC wrapper did not return to its VM caller')
                inputs=struct.unpack_from('<18Q',raw)
                assert inputs==(0x84000009,)+(0,)*17,'SMC received wrong function or nonzero arguments'
                assert struct.unpack_from('<Q',raw,144)[0]>>26==0x17,'Monitor did not receive AArch64 SMC'
                assert state==(1,0xffffffffffffffff,0x1818,0x1919,0x2929,0x4e001000,el*4),state
                results.append({'el':el,'system_reset_function':hex(inputs[0]),'unused_arguments_zero':True,
                    'signed_status':-1,'platform_register_stack_and_frame_restored':True,'physical_reset_tested':False})
                print(f'PASS compiled SMC wrapper EL{el}: arguments, signed error, stack and registers',flush=True)
        finally:
            if process and process.poll() is None:
                if q:
                    try:q.command('quit')
                    except (OSError,ValueError,RuntimeError):pass
                try:process.wait(timeout=3)
                except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=3)
            if q:q.close()
    (out/'result.json').write_text(json.dumps({'image_sha256':meta['image_sha256'],'tests':results},indent=2)+'\n')


if __name__=='__main__':main()
