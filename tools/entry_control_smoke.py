"""Test the C++ observer and original-entry handoff, stopping before the MTK kernel.

The VM fixture replaces the original kernel entry with a stop instruction.
Observer bytes are unchanged. This validates neither Android nor hardware boot.
"""
import argparse
import hashlib,json,os,pathlib,re,socket,struct,subprocess,time
from build_uefi import ROOT,llvm_dir,run
from probe_smoke import test_dtb
from vm_smoke import QMP

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',type=pathlib.Path,required=True)
    parser.add_argument('--build',type=pathlib.Path,default=ROOT/'build-entry-control/gta9wifi')
    parser.add_argument('--out',type=pathlib.Path,default=ROOT/'build-entry-control/vm-test')
    args=parser.parse_args()
    suffix='.exe' if os.name=='nt' else ''
    build=args.build.resolve()
    meta=json.loads((build/'build.json').read_text());image=(build/'Image').read_bytes()
    assert hashlib.sha256(image).hexdigest()==meta['image_sha256']
    marker=meta['runtime_marker'].encode();assert marker not in image
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    (out/'test.dtb').write_bytes(test_dtb());results=[]
    for level in (1,2):
     case=out/f'el{level}';case.mkdir(exist_ok=True)
     fixture=bytearray(image)
     # VM-only stop at the real kernel's first instruction: the MTK kernel itself
     # cannot initialize QEMU's unrelated virtual board. All observer bytes are exact.
     struct.pack_into('<I',fixture,meta['original_entry_offset'],0x14000000)
     (case/'fixture.img').write_bytes(fixture)
     source=f'''.text
    .global _start
    _start:
        ldr x1, =0x44000000
        ldr x2, =0x40000000
        ldr x3, ={len(image)}
    1:  ldrb w6, [x1], #1
        strb w6, [x2], #1
        subs x3, x3, #1
        b.ne 1b
        dsb sy
        ic iallu
        dsb sy
        isb
        ldr x0, =0x48000000
        ldr x1, =0x11223344
        ldr x2, =0x55667788
        ldr x3, =0x12345678
        mov sp, xzr
        ldr x9, =0x40000000
        br x9
        .ltorg
    '''
     # mov sp,xzr aliases an encoding not allowed for SP; use a scratch zero register.
     source=source.replace('mov sp, xzr','mov x10, #0\n    mov sp, x10')
     (case/'loader.S').write_text(source);llvm=llvm_dir()
     run([llvm/('clang'+suffix),'--target=aarch64-none-elf','-c',case/'loader.S','-o',case/'loader.o'])
     run([llvm/('ld.lld'+suffix),'-Ttext=0x4c000000','--entry=_start','-o',case/'loader.elf',case/'loader.o'])
     run([llvm/('llvm-objcopy'+suffix),'-O','binary',case/'loader.elf',case/'loader.bin'])
     with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
     command=[str(args.qemu.resolve()),'-name',f'kbl-entry-control-{time.time_ns()}',
      '-machine','virt,gic-version=3,virtualization='+('on' if level==2 else 'off'),'-cpu','cortex-a76','-m','1024','-smp','1',
      '-device',f'loader,file={case/"fixture.img"},addr=0x44000000,force-raw=on',
      '-device',f'loader,file={out/"test.dtb"},addr=0x48000000,force-raw=on',
      '-device',f'loader,file={case/"loader.bin"},addr=0x4c000000,cpu-num=0,force-raw=on',
      '-display','none','-monitor','none','-serial','none','-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off']
     q=None;p=None
     try:
      with (case/'stderr.log').open('wb') as err:
       p=subprocess.Popen(command,stdout=subprocess.DEVNULL,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
       q=QMP(port,time.monotonic()+10);deadline=time.monotonic()+20
       while time.monotonic()<deadline:
        time.sleep(.25);q.command('stop')
        regs=q.command('human-monitor-command',{'command-line':'info registers'})
        pc=int(re.search(r'PC=([0-9a-fA-F]+)',regs)[1],16)
        if pc==0x40000000+meta['original_entry_offset']:break
        q.command('cont')
       else:
        (case/'failed-registers.txt').write_text(regs);raise RuntimeError('original kernel entry not reached')
       (case/'registers.txt').write_text(regs)
       for register,value in [('X00',0x48000000),('X01',0x11223344),('X02',0x55667788),('X03',0x12345678)]:
        assert int(re.search(register+r'=([0-9a-fA-F]+)',regs)[1],16)==value,register
       assert int(re.search(r'SP=([0-9a-fA-F]+)',regs)[1],16)==0
       q.command('pmemsave',{'val':0x40000000,'size':len(image),'filename':str(case/'executed.img')})
       actual=(case/'executed.img').read_bytes();expected=bytearray(fixture)
       expected[meta['banner_offset']:meta['banner_offset']+len(marker)]=marker
       expected[meta['proc_marker_offset']:meta['proc_marker_offset']+7]=b'KBL_CPP'
       assert actual==expected,'memory changed beyond the runtime log/proc-version markers'
       results.append({'el':level,'stack_pointer_zero':True,'x0_x3_preserved':True,
        'original_kernel_entry_reached':True,'only_runtime_banner_changed':True})
       print('PASS entry control',level,'C++ marker + original entry + x0..x3, SP=0',flush=True)
     finally:
      if p and p.poll() is None:
       if q:
        try:q.command('quit')
        except Exception:pass
       try:p.wait(timeout=3)
       except subprocess.TimeoutExpired:p.terminate();p.wait(timeout=3)
      if q:q.close()
    (out/'result.json').write_text(json.dumps({'image_sha256':meta['image_sha256'],'tests':results},indent=2)+'\n')

if __name__=='__main__': main()
