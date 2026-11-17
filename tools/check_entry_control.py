"""Read the runtime-only pre-kernel C++ marker from normal Android over ADB."""
import argparse
import json
import re
from pathlib import Path
import subprocess

def check(adb, serial, output=None):
    def read(command):
        return subprocess.check_output([str(adb), '-s', serial, 'exec-out', command], timeout=15).decode(errors='replace').strip()
    model = read('getprop ro.product.model')
    loader = read('getprop ro.bootloader')
    if model != 'SM-X110' or loader != 'X110XXU4CYE5': raise ValueError('Unexpected device or bootloader')
    recovery = read('getprop ro.twrp.version')
    proc_version = read('cat /proc/version')
    # Accept already-installed builds from before the project was renamed.
    marker = re.match(r'^Linux ((?:KBL|BDK)_(?:CPP|E[12]M)) ',proc_version)
    trace = re.search(r'\[(?:KBL|BDK) ([^]]+)\]',proc_version)
    diagnostics = dict(piece.split('=',1) for piece in trace[1].split() if '=' in piece) if trace else None
    incoming_mmu = True if marker and marker[1].endswith('M') else None
    if diagnostics and 'sctlr' in diagnostics:
        try: incoming_mmu = bool(int(diagnostics['sctlr'],0)&1)
        except ValueError: pass
    result = {'model':model, 'bootloader':loader, 'separate_recovery_running':bool(recovery),
              'proc_version':proc_version, 'pre_kernel_cpp_executed':not recovery and bool(marker),
              'incoming_mmu_enabled':incoming_mmu,
              'pre_kernel_diagnostics':diagnostics,
              'framebuffer_write_completed':bool(diagnostics.get('fault')=='0x0' and
                  ((diagnostics.get('stage')=='0x6' and diagnostics.get('reason')=='drawn') or
                   (diagnostics.get('stage') in ('0x7','0x8','0x9') and diagnostics.get('reason')!='menu_draw_rejected'))) if diagnostics else None}
    if output:
        output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    if recovery:
        print('Recovery uses its own kernel. Check again after normal Android starts.')
    elif result['pre_kernel_cpp_executed']:
        print('Confirmed: the pre-kernel C++ control executed and continued into Android.')
    else:
        print('No entry-control marker in this kernel; execution is not confirmed.')
    return result

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb',type=Path,required=True)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--out',type=Path)
    args=parser.parse_args()
    check(args.adb,args.serial,args.out)
