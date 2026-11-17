#!/usr/bin/env bash
# Build an ARM64 Debian live userspace in a separate Linux build directory.
# No tablet access, flashing or host service installation is performed.
set -euo pipefail
project=$(cd -- "$(dirname -- "$0")/.." && pwd)
output=$(realpath -m -- "${1:?Usage: build_linux_desktop.sh NEW-output-directory}")
if [ "$(id -u)" != 0 ]; then printf 'Run inside an isolated Linux builder as root.\n' >&2; exit 1; fi
for command in mmdebstrap qemu-aarch64-static tar xz chroot; do command -v "$command" > /dev/null; done
if [ -e "$output" ]; then printf 'Output already exists; choose a new directory.\n' >&2; exit 1; fi
mkdir -p "$output"
packages=apt,ca-certificates,curl,busybox-static,dbus,udev,kmod,weston,seatd,firefox-esr,fonts-dejavu-core,libgl1-mesa-dri,libgl1,libegl1,libgles2,libosmesa6,iproute2,iw,wpasupplicant
mmdebstrap --architectures=arm64 --variant=minbase --include="$packages" \
    --aptopt='APT::Install-Recommends "false"' \
    bookworm "$output/rootfs" https://deb.debian.org/debian > "$output/bootstrap.log" 2>&1
rootfs="$output/rootfs"
install -D -m 0644 "$project/targets/samsung/gta9wifi/linux/desktop/welcome.html" "$rootfs/usr/share/kbl/welcome.html"
install -D -m 0755 "$project/targets/samsung/gta9wifi/linux/desktop/start-desktop.sh" "$rootfs/usr/local/bin/kbl-desktop"
printf 'kbl-live\n' > "$rootfs/etc/hostname"
for group in video input render; do
    chroot "$rootfs" /usr/bin/getent group "$group" > /dev/null || chroot "$rootfs" /usr/sbin/groupadd --system "$group"
done
chroot "$rootfs" /usr/sbin/useradd --create-home --uid 1000 --groups video,input,render --shell /bin/bash kbl
chroot "$rootfs" /usr/sbin/usermod --lock kbl
chroot "$rootfs" /usr/bin/dpkg-query -W '-f=${Package}\t${Version}\t${Architecture}\n' > "$output/packages.tsv"
# Keep boot identity machine-local and avoid shipping builder resolver state.
: > "$rootfs/etc/machine-id"
: > "$rootfs/etc/resolv.conf"
find "$rootfs/var/cache/apt/archives" -maxdepth 1 -type f -name '*.deb' -delete
tar --sort=name --numeric-owner --acls --xattrs -C "$rootfs" -cf - . | xz -T2 -8 > "$output/rootfs.tar.xz"
sha256sum "$output/rootfs.tar.xz" > "$output/SHA256SUMS.txt"
du -sh "$rootfs"
stat -c 'Compressed desktop bytes: %s' "$output/rootfs.tar.xz"
printf 'ARM64 desktop userspace built. Device boot and peripherals still require validation.\n'
