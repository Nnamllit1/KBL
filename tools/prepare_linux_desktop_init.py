"""Prepare a native desktop initramfs from authenticated CYE5 recovery inputs.

External kernel modules/firmware remain in the generated output, not source.
No tablet access, partition changes or flashing are performed.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct

from build_linux_init import archive
from build_uefi import ROOT
from package_probe import KNOWN_CYE5_BOOTS,parse


def sha(data):return hashlib.sha256(data).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inputs',required=True,type=Path,help='Prepared SM-X110 CYE5 recovery prebuilt directory')
    p.add_argument('--source-boot',required=True,type=Path)
    p.add_argument('--busybox',required=True,type=Path,help='Static ARM64 BusyBox from the desktop rootfs')
    p.add_argument('--rootfs',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path)
    a=p.parse_args()
    if a.out.exists():raise ValueError('Use a new output directory')
    boot=a.source_boot.read_bytes()
    if sha(boot) not in KNOWN_CYE5_BOOTS:raise ValueError('Unsupported CYE5 BOOT input')
    original=gzip.decompress(parse(boot)['kernel'])
    kernel=gzip.decompress((a.inputs/'kernel').read_bytes())
    if sha(kernel)!='35c551aaf32c62cfd13e79ccd6e4dab73f22d0cff8ad83d65a6e3ec521ee72ef':
        raise ValueError('Recovery kernel is not the pinned CYE5 kernel')
    if len(original)!=len(kernel):raise ValueError('Kernel size differs')
    changed={i for i,(x,y) in enumerate(zip(original,kernel)) if x!=y}
    if changed not in (set(),{5014840,5014841,5014842}):raise ValueError('Kernel differs outside the supported Magisk branch patch')
    release=re.search(rb'Linux version (\d+\.\d+\.\d+[^ ]*) ',kernel).group(1).decode()
    metadata=json.loads((a.inputs/'inputs.json').read_text())
    if metadata['model']!='SM-X110' or metadata['build']!='X110XXU4CYE5':raise ValueError('Wrong recovery inputs')
    listed={entry['path']:entry for entry in metadata['files']}
    payload={}
    for f in sorted((a.inputs/'ramdisk').rglob('*')):
        if not f.is_file():continue
        rel=f.relative_to(a.inputs/'ramdisk').as_posix()
        if not (rel.startswith('lib/modules/') or rel.startswith('vendor/firmware/')):continue
        data=f.read_bytes();entry=listed.get('ramdisk/'+rel)
        if not entry or sha(data)!=entry['sha256']:raise ValueError('Changed recovery input '+rel)
        if f.suffix=='.ko':
            vermagic=re.search(rb'(?:^|\x00)vermagic=([^\x00]+)',data)
            if not vermagic or vermagic.group(1).split()[0].decode()!=release:raise ValueError('Wrong module ABI '+rel)
            rel='lib/modules/'+release+'/'+f.name
        elif rel.startswith('lib/modules/'):continue
        payload[rel]=(data,0o100644)
    modules=a.inputs/'ramdisk/lib/modules'
    dependencies={Path(name).name:[Path(n).name for n in children.split()]
                  for line in (modules/'modules.dep').read_text().splitlines() if ':' in line
                  for name,children in [line.split(':',1)]}
    blocked={'mtk_wdt.ko','softdog.ko','aee_hangdet.ko','monitor_hang.ko'}
    seeds=[n.strip() for n in (modules/'modules.load.recovery').read_text().splitlines() if n.strip() and not n.startswith('#')]
    seeds+=[n for n in ('touch_common.ko','novatek.ko','musb_main.ko','musb_hdrc.ko','extcon-mtk-usb.ko') if (modules/n).is_file()]
    ordered=[];seen=set()
    def visit(name,stack=()):
        if name in blocked:return
        if name in seen:return
        if name in stack:raise ValueError('Module dependency cycle')
        if not (modules/name).is_file():raise ValueError('Missing module '+name)
        for dep in dependencies.get(name,[]):
            if dep in blocked:raise ValueError('Required driver depends on excluded watchdog/monitor '+name)
            visit(dep,(*stack,name))
        seen.add(name);ordered.append(name)
    for name in seeds:visit(name)
    busybox=a.busybox.read_bytes()
    if len(busybox)<64 or busybox[:6]!=b'\x7fELF\x02\x01' or busybox[18:20]!=b'\xb7\x00':
        raise ValueError('BusyBox must be a static little-endian ARM64 ELF')
    phoff=struct.unpack_from('<Q',busybox,32)[0]
    phsize,phcount=struct.unpack_from('<HH',busybox,54)
    if phsize<56 or phoff+phsize*phcount>len(busybox) or not phcount:
        raise ValueError('Invalid BusyBox program headers')
    if any(struct.unpack_from('<I',busybox,phoff+phsize*i)[0]==3 for i in range(phcount)):
        raise ValueError('BusyBox requires a dynamic interpreter')
    rootfs_hash=sha(a.rootfs.read_bytes());rootfs_name='rootfs-'+rootfs_hash[:12]+'.tar.xz'
    init=(ROOT/'targets/samsung/gta9wifi/linux/desktop/boot-init.sh').read_text()
    for old,new in {'@KERNEL_RELEASE@':release,'@ROOTFS_FILENAME@':rootfs_name,'@ROOTFS_SHA256@':rootfs_hash}.items():init=init.replace(old,new)
    payload.update({'init':(init.encode(),0o100755),'bin/busybox':(busybox,0o100755),
        'etc/kbl-module-plan':(('\n'.join(ordered)+'\n').encode(),0o100644),
        'etc/kbl-live-init':((ROOT/'targets/samsung/gta9wifi/linux/desktop/live-init.sh').read_bytes(),0o100755)})
    dirs={'dev','proc','sys','cache','newroot','run','sbin'}
    for name in payload:
        dirs.update(parent.as_posix() for parent in Path(name).parents if str(parent)!='.')
    entries=[(name,b'',0o040755,0,0) for name in sorted(dirs,key=lambda n:(n.count('/'),n))]
    entries+=[(name,data,mode,0,0) for name,(data,mode) in sorted(payload.items())]
    entries.append(('dev/console',b'',0o020600,5,1))
    packed=gzip.compress(bytes(archive(entries)),compresslevel=9,mtime=0)
    if len(packed)>16*1024*1024:raise ValueError('Desktop initramfs exceeds the loader limit')
    a.out.mkdir(parents=True);(a.out/'initramfs.cpio.gz').write_bytes(packed)
    report={'kind':'samsung-kernel-native-linux-desktop','kernel_release':release,'source_boot_sha256':sha(boot),
        'initramfs_sha256':sha(packed),'initramfs_bytes':len(packed),'module_plan':ordered,
        'excluded_watchdog_monitors':sorted(blocked),'rootfs_sha256':rootfs_hash,'rootfs_filename':rootfs_name,
        'rootfs_bytes':a.rootfs.stat().st_size,'contains_external_firmware_inputs':True,'hardware_desktop_verified':False}
    (a.out/'desktop.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Prepared matched-driver Linux desktop initramfs:',a.out/'initramfs.cpio.gz')


if __name__=='__main__':main()
