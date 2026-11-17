"""Build an upstream Linux live initramfs from static ARM64 BusyBox."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
from build_linux_init import archive
from build_uefi import ROOT


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--busybox',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    if a.out.exists():raise ValueError('Use a new output directory')
    busybox=a.busybox.read_bytes()
    if len(busybox)<64 or busybox[:6]!=b'\x7fELF\x02\x01' or busybox[18:20]!=b'\xb7\x00':
        raise ValueError('Expected little-endian ARM64 ELF BusyBox')
    offset=struct.unpack_from('<Q',busybox,32)[0];size,count=struct.unpack_from('<HH',busybox,54)
    if size<56 or not count or offset+size*count>len(busybox):raise ValueError('Invalid ELF program headers')
    if any(struct.unpack_from('<I',busybox,offset+size*i)[0]==3 for i in range(count)):
        raise ValueError('BusyBox must be statically linked')
    script=(ROOT/'targets/samsung/gta9wifi/linux/live-init.sh').read_text().encode()
    entries=[(name,b'',0o040755,0,0) for name in ['bin','dev','proc','sys','run','tmp']]
    entries+=[('init',script,0o100755,0,0),('bin/busybox',busybox,0o100755,0,0),('dev/console',b'',0o020600,5,1)]
    packed=gzip.compress(bytes(archive(entries)),mtime=0)
    a.out.mkdir(parents=True);(a.out/'initramfs.cpio.gz').write_bytes(packed)
    result={'kind':'upstream-linux-live-init','initramfs_sha256':hashlib.sha256(packed).hexdigest(),
        'busybox_sha256':hashlib.sha256(busybox).hexdigest(),'mounts_android_filesystems':False,
        'includes_vendor_drivers':False,'desktop_provided':False}
    (a.out/'init.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Prepared native upstream Linux live initramfs:',a.out)


if __name__=='__main__':main()
