#!/bin/busybox sh
# Native upstream Linux PID 1. No Android filesystem is mounted or modified.
set -u
if [ "$$" != 1 ]; then echo 'This script requires Linux PID 1.' >&2;exit 1;fi
/bin/busybox --install -s /bin
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /run /tmp
mount -t devpts devpts /dev/pts
mount -t tmpfs -o mode=755,size=32m tmpfs /run
mount -t tmpfs -o mode=1777,size=64m tmpfs /tmp
exec >/dev/console 2>&1
echo 'KBL_UPSTREAM_LIVE_INIT_OK: native Linux userspace started.' > /dev/kmsg
echo "KBL_UPSTREAM_KERNEL: $(uname -r)" > /dev/kmsg
uname -a
echo 'Upstream Linux live system. Android filesystems are not mounted.'
echo 'Display and input devices:'
ls -l /dev/dri /dev/fb* /dev/input 2>/dev/null || true
echo 'USB controllers:'
ls /sys/class/udc /sys/bus/usb/devices 2>/dev/null || true
# The retained console is the progress view; the selected system does not yet
# provide USB ADB, networking, touch or the Debian desktop archive.
while :; do
    echo 'KBL_UPSTREAM_LIVE_ALIVE: waiting for device bring-up.' > /dev/kmsg
    sleep 15
done
