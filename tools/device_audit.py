"""Read-only target audit over ADB. No reboot, block write or credential request."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]

def audit(adb, serial, profile, output):
    def run(command):
        p = subprocess.run([str(adb), '-s', serial, 'exec-out', command],
                           capture_output=True, timeout=20)
        if p.returncode:
            raise RuntimeError(p.stderr.decode(errors='replace'))
        return p.stdout
    model = run('getprop ro.product.model').decode().strip()
    loader = run('getprop ro.bootloader').decode().strip()
    if model != profile['model'] or loader != profile['reference_bootloader']:
        raise RuntimeError('Device/build does not match the selected profile; stopped')
    uid = run('id -u').decode().strip()
    def root(command):
        return run(command if uid == '0' else '/debug_ramdisk/su -c ' + shlex.quote(command))
    if root('id -u').strip() != b'0':
        raise RuntimeError('Root ADB or an existing Android root grant is required for the audit')
    config = gzip.decompress(root('cat /proc/config.gz')).decode()
    wanted = ('KEXEC', 'KEXEC_FILE', 'EFI', 'EFI_STUB', 'OF', 'DRM', 'FB')
    options = {}
    for key in wanted:
        for line in config.splitlines():
            if line.startswith('CONFIG_' + key + '='):
                options[key] = line.split('=', 1)[1]
            elif line == '# CONFIG_' + key + ' is not set':
                options[key] = 'n'
    sizes = {}
    for partition in ('boot', 'recovery'):
        raw = root('blockdev --getsize64 /dev/block/by-name/' + partition).decode().strip()
        if not raw.isdecimal():
            raise RuntimeError('Unable to read ' + partition + ' partition size')
        sizes[partition] = int(raw)
    if sizes != {'boot': profile['boot_partition_bytes'], 'recovery': profile['recovery_partition_bytes']}:
        raise RuntimeError('Partition geometry differs from profile; stopped')
    # Treat all artifacts as local research files. The live DT can contain
    # device-specific identifiers and entropy; do not export it to public docs.
    dtb = root('cat /sys/firmware/fdt')
    if not dtb.startswith(b'\xd0\x0d\xfe\xed'):
        raise RuntimeError('No valid live flattened device tree available')
    iomem = root('cat /proc/iomem')
    if b'00000000-00000000' in iomem or b'System RAM' not in iomem:
        raise RuntimeError('Physical memory addresses are hidden or unavailable')
    output.mkdir(parents=True, exist_ok=False)
    (output / 'kernel.config').write_text(config)
    (output / 'live.dtb').write_bytes(dtb)
    (output / 'iomem.txt').write_bytes(iomem)
    result = {
        'time_utc': datetime.now(timezone.utc).isoformat(),
        'profile': profile['id'], 'model': model, 'bootloader': loader,
        'kernel': run('uname -r').decode().strip(),
        'recovery_version': run('getprop ro.twrp.version').decode().strip() or None,
        'kernel_options': options, 'partition_bytes': sizes,
        'live_dtb_sha256': hashlib.sha256(dtb).hexdigest(),
        'device_writes': False, 'reboot_requested': False,
        'device_boot_validated': False,
    }
    (output / 'audit.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True, type=Path)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--profile', type=Path, default=ROOT / 'targets/samsung/gta9wifi/profile.json')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    audit(args.adb, args.serial, json.loads(args.profile.read_text()), args.out)
