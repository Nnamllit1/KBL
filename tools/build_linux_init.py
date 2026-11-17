"""Build the freestanding Linux PID 1 and a deterministic newc initramfs."""
import argparse
import hashlib
import json
import os
from pathlib import Path

from build_uefi import ROOT,llvm_dir,run


def archive(files):
    result=bytearray()
    for inode,(name,data,mode,major,minor) in enumerate(files+[('TRAILER!!!',b'',0,0,0)],1):
        name=name.encode()+b'\0'
        fields=[inode,mode,0,0,1,0,len(data),0,0,major,minor,len(name),0]
        result+=b'070701'+b''.join(f'{n:08x}'.encode() for n in fields)+name
        result+=b'\0'*(-len(result)%4);result+=data;result+=b'\0'*(-len(result)%4)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path);p.add_argument('--llvm-bin')
    args=p.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    llvm=llvm_dir(args.llvm_bin);suffix='.exe' if os.name=='nt' else ''
    run([llvm/('clang++'+suffix),'--target=aarch64-linux-gnu','-std=c++17','-O2','-ffreestanding','-fno-builtin',
        '-fno-exceptions','-fno-rtti','-fno-stack-protector','-mgeneral-regs-only','-mstrict-align','-nostdlib','-static',
        '-fno-pie','-fuse-ld=lld','-Wl,-e,_start,--build-id=none','-Wall','-Wextra','-Werror',
        ROOT/'targets/samsung/gta9wifi/linux/init.cpp','-o',out/'init'])
    data=archive([('init',(out/'init').read_bytes(),0o100755,0,0),
        ('dev',b'',0o040755,0,0),('dev/console',b'',0o020600,5,1),('dev/kmsg',b'',0o020600,1,11),
        ('proc',b'',0o040755,0,0),('sys',b'',0o040755,0,0),('tmp',b'',0o041777,0,0)])
    (out/'initramfs.cpio').write_bytes(data)
    metadata={'kind':'independent-linux-pid1','init_sha256':hashlib.sha256((out/'init').read_bytes()).hexdigest(),
        'initramfs_sha256':hashlib.sha256(data).hexdigest(),'mounts_android_storage':False}
    (out/'init.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print('Built independent Linux PID 1:',out/'initramfs.cpio')


if __name__=='__main__':main()
