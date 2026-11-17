#!/bin/sh
# Native live-system PID 1; never run inside the normal Android environment.
set -u
if [ "$$" != 1 ]; then printf 'This script must run as Linux PID 1.\n' >&2;exit 1;fi
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
log=/mnt/kbl-cache/kbl-linux/last-desktop.log
exec >>"$log" 2>&1
printf 'KBL_DESKTOP_ROOT: independent Debian userspace\n'
# The initramfs contains only a coldplug device scan. Give udev the live tree
# before handing DRM and input descriptors to the unprivileged session.
mount -t tmpfs -o mode=755,size=64m tmpfs /run
mount -t tmpfs -o mode=1777,size=256m tmpfs /tmp
mkdir -p /dev/pts /dev/shm /run/dbus /run/user/1000
mount -t devpts -o mode=620,ptmxmode=666 devpts /dev/pts
ln -sf pts/ptmx /dev/ptmx
mount -t tmpfs -o mode=1777,size=256m tmpfs /dev/shm
ln -sf /proc/self/fd /dev/fd
ln -sf /proc/self/fd/0 /dev/stdin
ln -sf /proc/self/fd/1 /dev/stdout
ln -sf /proc/self/fd/2 /dev/stderr
chmod 700 /run/user/1000;chown 1000:1000 /run/user/1000
depmod -a || true
/lib/systemd/systemd-udevd --daemon
udevadm trigger;udevadm settle --timeout=10 || true
dbus-uuidgen --ensure=/etc/machine-id
dbus-daemon --system --fork || true
export SEATD_VTBOUND=0
seatd -g video &
seat=$!
attempt=0
while [ ! -S /run/seatd.sock ] && [ "$attempt" -lt 50 ];do attempt=$((attempt + 1));sleep .1;done
printf 'KBL_DESKTOP_ROOT: starting software-rendered desktop\n'
runuser -u kbl -- env HOME=/home/kbl XDG_RUNTIME_DIR=/run/user/1000 \
    dbus-run-session /usr/local/bin/kbl-desktop
printf 'KBL_DESKTOP_EXIT: desktop process returned\n'
kill "$seat" 2>/dev/null || true
dmesg > /mnt/kbl-cache/kbl-linux/last-kernel.log
sync
sleep 5
/bin/busybox reboot -f
while :;do sleep 30;done
