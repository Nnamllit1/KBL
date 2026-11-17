"""Test display drawing, fault return and CPU-state restoration before kernel entry."""
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
from build_uefi import ROOT,llvm_dir,run
from probe_smoke import test_dtb,png,BASE,FRAME
from vm_smoke import QMP

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',required=True,type=Path)
    parser.add_argument('--build',type=Path,default=ROOT/'build-display-bridge/gta9wifi')
    parser.add_argument('--out',type=Path,default=ROOT/'build-display-bridge/vm-test')
    parser.add_argument('--bad-fdt',action='store_true')
    parser.add_argument('--bad-psci',choices=['unknown','unterminated'])
    parser.add_argument('--menu-case',choices=['timeout','android','recovery','download','busy','carried','linux-rejected','linux-shared'])
    parser.add_argument('--expected-default-index',type=int,default=0)
    parser.add_argument('--expected-timeout-ms',type=int,default=8000)
    args=parser.parse_args();build=args.build.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    meta=json.loads((build/'build.json').read_text());image=(build/'Image').read_bytes()
    assert meta['kind'] in ('pre-kernel-display-bridge','pre-kernel-boot-menu') and hashlib.sha256(image).hexdigest()==meta['image_sha256']
    menu_enabled=meta.get('menu_enabled',False)
    if args.menu_case: assert menu_enabled and meta.get('menu_vm_bus_enabled'),'Use the VM-only --boot-menu --menu-test build'
    dtb=bytearray(test_dtb(controls=bool(args.menu_case),
        psci_method={'unknown':b'other\0','unterminated':b'smc'}.get(args.bad_psci,b'smc\0')))
    shared_case=args.menu_case=='linux-shared'
    if shared_case:
        from fdt_fixture import decode,encode,Node
        root,reservations,boot=decode(dtb)
        root.children.append(Node('reserved-memory',{'#address-cells':struct.pack('>I',2),
            '#size-cells':struct.pack('>I',2),'ranges':b''},[
            Node('framebuffer',{'reg':struct.pack('>QQ',BASE,0xc50000)})]))
        dtb=bytearray(encode(root,reservations,boot))
    if args.bad_fdt:dtb[0]=0
    (out/'test.dtb').write_bytes(dtb)
    events = {'timeout':[], 'android':[(300,4),(500,0)],
        'recovery':[(300,2),(500,0),(700,4),(900,0)],
        'download':[(300,1),(500,0),(700,4),(900,0)],
        'busy':[], 'carried':[(0,4),(500,0)]}.get(args.menu_case,[])
    if meta.get('linux_payload'):
        if args.menu_case in ('linux-rejected','linux-shared'):events=[(300,2),(500,0),(700,4),(900,0)]
        if args.menu_case=='recovery':events=[(300,2),(500,0),(700,2),(900,0),(1100,4),(1300,0)]
    bus=struct.pack('<4I',len(events),int(args.menu_case=='busy'),0,0)
    bus+=b''.join(struct.pack('<II',*event) for event in events)
    (out/'menu-bus.bin').write_bytes(bus)
    llvm=llvm_dir();suffix='.exe' if os.name=='nt' else '';results=[]
    def assemble(folder,name,source,base):
        (folder/(name+'.S')).write_text(source)
        run([llvm/('clang'+suffix),'--target=aarch64-none-elf','-c',folder/(name+'.S'),'-o',folder/(name+'.o')])
        run([llvm/('ld.lld'+suffix),'-Ttext='+hex(base),'--entry=_start','-o',folder/(name+'.elf'),folder/(name+'.o')])
        run([llvm/('llvm-objcopy'+suffix),'-O','binary',folder/(name+'.elf'),folder/(name+'.bin')])
        return (folder/(name+'.bin')).read_bytes()
    for el,selection in [(1,0),(2,1)]:
        case=out/f'el{el}-sp{selection}';case.mkdir(exist_ok=True)
        entry=0x40000000+meta['original_entry_offset']
        stop=assemble(case,'stop',f'''.text
.global _start
_start:
 ldr x10, =0x51000000
 mov x11, sp
 str x11, [x10]
 mrs x11, CurrentEL
 str x11, [x10,#8]
 mrs x11, SPSel
 str x11, [x10,#16]
 mrs x11, DAIF
 str x11, [x10,#24]
 mrs x11, {'vbar_el1' if el==1 else 'vbar_el2'}
 str x11, [x10,#32]
 stp x0,x1,[x10,#40]
 stp x2,x3,[x10,#56]
1: b 1b
 .ltorg
''',entry)
        fixture=bytearray(image);fixture[meta['original_entry_offset']:meta['original_entry_offset']+len(stop)]=stop
        (case/'fixture.img').write_bytes(fixture)
        assemble(case,'loader',f'''.text
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
 msr SPSel,#0
 ldr x10,=0x4e000000
 mov sp,x10
 msr SPSel,#1
 ldr x10,=0x4e001000
 mov sp,x10
 ldr x10,=0x4e100000
 msr {'vbar_el1' if el==1 else 'vbar_el2'},x10
 msr SPSel,#{selection}
 msr DAIFClr,#1
 mrs x10,{'sctlr_el1' if el==1 else 'sctlr_el2'}
 orr x10,x10,#2
 msr {'sctlr_el1' if el==1 else 'sctlr_el2'},x10
 isb
 ldr x0,=0x48000000
 ldr x1,=0x11223344
 ldr x2,=0x55667788
 ldr x3,=0x12345678
 ldr x9,=0x40000000
 br x9
 .ltorg
''',0x4c000000)
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        command=[str(args.qemu.resolve()),'-name',f'kbl-display-bridge-{time.time_ns()}',
          '-machine','virt,gic-version=3,virtualization='+('on' if el==2 else 'off'),'-cpu','cortex-a76','-m','1024','-smp','1',
          '-device',f'loader,file={case/"fixture.img"},addr=0x60000000,force-raw=on',
          '-device',f'loader,file={out/"test.dtb"},addr=0x48000000,force-raw=on',
          '-device',f'loader,file={out/"menu-bus.bin"},addr=0x52000000,force-raw=on',
          '-device',f'loader,file={case/"loader.bin"},addr=0x4c000000,cpu-num=0,force-raw=on',
          '-display','none','-monitor','none','-serial','none','-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off']
        q=None;process=None
        try:
            with (case/'stderr.log').open('wb') as errors:
                process=subprocess.Popen(command,stdout=subprocess.DEVNULL,stderr=errors,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                q=QMP(port,time.monotonic()+10);deadline=time.monotonic()+25
                while time.monotonic()<deadline:
                    time.sleep(.2);q.command('stop')
                    q.command('pmemsave',{'val':0x51000000,'size':72,'filename':str(case/'cpu-state.bin')})
                    state=struct.unpack('<9Q',(case/'cpu-state.bin').read_bytes())
                    if state[1]==el*4:break
                    q.command('cont')
                else:
                    (case/'failed-registers.txt').write_text(q.command('human-monitor-command',{'command-line':'info registers'}))
                    raise RuntimeError('Original kernel entry not reached')
                q.command('pmemsave',{'val':0x40000000,'size':len(image),'filename':str(case/'executed.img')})
                actual=(case/'executed.img').read_bytes()
                context_offset=meta['symbols']['bridge_context']-0x40000000
                ctx=struct.unpack_from('<35Q',actual,context_offset)
                assert state[0]==(0x4e000000 if selection==0 else 0x4e001000)
                assert state[2]==selection and state[3]==ctx[8]==0x380 and state[4]==ctx[9]==0x4e100000
                if shared_case:
                    assert meta['linux_payload']['shares_android_kernel']
                    assert list(state[5:9])==[meta['symbols']['linux_dtb_buffer'],0,0,0]
                else:assert list(state[5:9])==[0x48000000,0x11223344,0x55667788,0x12345678]
                fmt_start=meta['proc_format_offset'];capacity=meta['proc_format_capacity']
                fmt=actual[fmt_start:fmt_start+capacity].split(b'\0')[0]
                (case/'trace.txt').write_bytes(fmt+b'\n')
                assert fmt.startswith(b'%s KBL_CPP %s [KBL ') and fmt.count(b'%s')==3
                before=bytearray(fixture[:meta['source_kernel_file_bytes']])
                before[meta['banner_offset']:meta['banner_offset']+14]=b'KBL bootstrap '
                before[fmt_start:fmt_start+len(fmt)+1]=fmt+b'\0'
                assert actual[:len(before)]==before,'Unexpected change to original kernel bytes'
                if args.bad_fdt:
                    assert ctx[13]==2 and ctx[18]==0 and b'reason=invalid_FDT_header' in fmt
                elif meta.get('reset_fault_test_enabled'):
                    assert args.menu_case in ('recovery','download')
                    assert ctx[13]==8 and ctx[18]==1 and ctx[26]==0 and ctx[15]>>26==0x3c and b'reason=exception' in fmt
                    q.command('pmemsave',{'val':0x52000400,'size':20,'filename':str(case/'restored-registers.bin')})
                    restored=struct.unpack('<5I',(case/'restored-registers.bin').read_bytes())
                    assert restored==(0x22000017,0xa5a50000,0x66262564,0,0xcafebabe),'Fault cleanup left changed reboot registers or SRAM markers'
                elif meta['fault_test_enabled']:
                    assert ctx[13]==4 and ctx[18]==1 and ctx[15]>>26==0x3c and b'reason=exception' in fmt
                elif menu_enabled:
                    if not args.menu_case or args.bad_psci:
                        assert ctx[13]==6 and ctx[18]==0 and b'reason=control_layout_rejected' in fmt
                    elif args.menu_case=='busy':
                        assert ctx[13]==7 and ctx[18]==0 and b'reason=input_unavailable' in fmt
                    else:
                        assert ctx[13]==(10 if shared_case else 9) and ctx[18]==0 and ctx[26]==(1 if shared_case else 0)
                        if shared_case:
                            assert b'reason=linux_shared_kernel' in fmt and ctx[31]==1
                            new_tree=ctx[0]-0x40000000
                            size=int.from_bytes(actual[new_tree+4:new_tree+8],'big')
                            prepared=actual[new_tree:new_tree+size]
                            from fdt_fixture import decode
                            t,reservations,_=decode(prepared)
                            assert t.child('chosen').props['linux,initrd-start']==struct.pack('>Q',meta['symbols']['linux_initrd_begin'])
                            assert 'no-map' not in t.child('reserved-memory').child('framebuffer').props
                            assert any(a==0x40000000+meta['observer_offset'] for a,n in reservations)
                            q.command('pmemsave',{'val':0x48000000,'size':len(dtb),'filename':str(case/'unchanged-live.dtb')})
                            assert (case/'unchanged-live.dtb').read_bytes()==dtb
                        elif args.menu_case in ('timeout','carried'):
                            assert b'reason=android_timeout' in fmt and ctx[31]==args.expected_default_index and ctx[33]>=args.expected_timeout_ms
                        elif args.menu_case=='android':
                            assert b'reason=android_selected' in fmt and ctx[31]==0 and ctx[33]<8000
                        elif args.menu_case=='linux-rejected':
                            assert b'reason=unsupported_reserved-memory_layout' in fmt and ctx[31]==1 and not ctx[34]
                        else:
                            assert b'reason=firmware_reset_returned' in fmt and ctx[34]==0xffffffffffffffff
                            assert ctx[31]==((2 if meta.get('linux_payload') else 1) if args.menu_case=='recovery' else (3 if meta.get('linux_payload') else 2))
                            q.command('pmemsave',{'val':0x52000000,'size':16,'filename':str(case/'reset-record.bin')})
                            record=struct.unpack('<4I',(case/'reset-record.bin').read_bytes())
                            assert record[2]==(0xa5a50002 if args.menu_case=='recovery' else 0xa5a50000) and record[3]==0x84000009
                            q.command('pmemsave',{'val':0x52000420,'size':24,'filename':str(case/'requested-reset.bin')})
                            requested=struct.unpack('<6I',(case/'requested-reset.bin').read_bytes())
                            assert requested==(record[2],0,0x12345678,
                                0x12345674 if args.menu_case=='recovery' else 0x12345671,0x22000016,0x84000009)
                            q.command('pmemsave',{'val':0x52000400,'size':20,'filename':str(case/'restored-registers.bin')})
                            assert struct.unpack('<5I',(case/'restored-registers.bin').read_bytes())==(
                                0x22000017,0xa5a50000,0x66262564,0,0xcafebabe)
                    if args.menu_case in ('timeout','carried','android','busy','linux-rejected','linux-shared') or args.bad_psci:
                        q.command('pmemsave',{'val':0x52000000,'size':16,'filename':str(case/'reset-record.bin')})
                        assert struct.unpack('<4I',(case/'reset-record.bin').read_bytes())[2:]==(0,0),'Unexpected firmware reset request'
                    q.command('pmemsave',{'val':BASE-16,'size':FRAME*3+32,'filename':str(case/'framebuffer.bin')})
                    pixels=(case/'framebuffer.bin').read_bytes();assert not any(pixels[:16]+pixels[-16:])
                    planes=pixels[16:-16];assert planes[:FRAME]==planes[FRAME:2*FRAME]==planes[2*FRAME:]
                    png(case/'screen.png',planes[:800*1340*4])
                else:
                    assert ctx[13]==6 and ctx[18]==0 and b'reason=drawn' in fmt
                    q.command('pmemsave',{'val':BASE-16,'size':FRAME*3+32,'filename':str(case/'framebuffer.bin')})
                    pixels=(case/'framebuffer.bin').read_bytes();assert not any(pixels[:16]+pixels[-16:])
                    planes=pixels[16:-16];assert planes[:FRAME]==planes[FRAME:2*FRAME]==planes[2*FRAME:]
                    assert struct.unpack_from('<I',planes,(860*800+100)*4)[0]==0xffff4040
                    png(case/'screen.png',planes[:800*1340*4])
                (case/'trace.txt').write_bytes(fmt+b'\n')
                results.append({'el':el,'initial_sp_selection':selection,'cpu_state_restored':True,
                    'kernel_bytes_preserved_except_markers':True,'stage':ctx[13],'fault':ctx[18],
                    'format':fmt.decode()})
                print(f'PASS bridge EL{el}, SPsel={selection}: '+fmt.decode(),flush=True)
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
