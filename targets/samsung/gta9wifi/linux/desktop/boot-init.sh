#!/bin/busybox sh
set -u
BB=/bin/busybox
if [ "$$" != 1 ]; then printf 'This init script must run as Linux PID 1.\n' >&2; exit 1; fi
$BB --install -s /bin
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t tmpfs -o mode=755,size=32m tmpfs /dev
mknod /dev/console c 5 1;mknod /dev/kmsg c 1 11
mknod /dev/null c 1 3;chmod 666 /dev/null
mknod /dev/zero c 1 5;chmod 666 /dev/zero
mknod /dev/random c 1 8;chmod 666 /dev/random
mknod /dev/urandom c 1 9;chmod 666 /dev/urandom
exec >/dev/kmsg 2>&1
printf 'KBL_DESKTOP_INIT: loading matched hardware drivers\n'
while read -r module; do
    [ -n "$module" ] || continue
    insmod "/lib/modules/@KERNEL_RELEASE@/$module" || printf 'KBL_MODULE_FAILED: %s\n' "$module"
done < /etc/kbl-module-plan
mdev -s
cache_device=
attempt=0
while [ "$attempt" -lt 30 ]; do
    for entry in /sys/class/block/*/uevent; do
        if grep -qx 'PARTNAME=cache' "$entry"; then
            node=${entry%/uevent};node=${node##*/};cache_device=/dev/$node;break
        fi
    done
    [ -n "$cache_device" ] && [ -b "$cache_device" ] && break
    attempt=$((attempt + 1));sleep 1;mdev -s
done
fail() {
    printf 'KBL_DESKTOP_FAILURE: %s\n' "$*"
    [ ! -d /cache/kbl-linux ] || dmesg > /cache/kbl-linux/last-kernel.log
    sync
    sleep 5
    reboot -f
    while :; do sleep 30;done
}
[ -n "$cache_device" ] || fail 'cache partition was not enumerated'
mount -t ext4 -o rw "$cache_device" /cache || fail 'cache mount failed'
mkdir -p /cache/kbl-linux
printf 'KBL desktop init started\n' > /cache/kbl-linux/last-boot.log
archive=/cache/kbl-linux/@ROOTFS_FILENAME@
[ -f "$archive" ] || fail 'desktop archive is missing'
printf '@ROOTFS_SHA256@  %s\n' "$archive" | sha256sum -c - || fail 'desktop archive checksum mismatch'
mount -t tmpfs -o mode=755,size=65% tmpfs /newroot || fail 'RAM root mount failed'
printf 'KBL_DESKTOP_INIT: extracting independent Linux userspace\n'
tar -xJf "$archive" -C /newroot || fail 'desktop extraction failed'
mkdir -p /newroot/lib/modules /newroot/vendor/firmware /newroot/mnt/kbl-cache /newroot/usr/local/sbin
cp -a /lib/modules/. /newroot/lib/modules/
if [ -d /vendor/firmware ]; then cp -a /vendor/firmware/. /newroot/vendor/firmware/;fi
cp /etc/kbl-live-init /newroot/usr/local/sbin/kbl-live-init
chmod 755 /newroot/usr/local/sbin/kbl-live-init
dmesg > /cache/kbl-linux/last-kernel.log
mount --move /dev /newroot/dev
mount --move /sys /newroot/sys
mount --move /proc /newroot/proc
mount --move /cache /newroot/mnt/kbl-cache
exec "$BB" switch_root /newroot /usr/local/sbin/kbl-live-init
