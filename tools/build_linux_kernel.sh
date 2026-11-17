#!/usr/bin/env bash
# Build an upstream ARM64 diagnostic kernel. Run inside a Linux build machine.
set -euo pipefail
project=$(cd -- "$(dirname -- "$0")/.." && pwd)
source_dir=$(realpath -- "${1:?Usage: build_linux_kernel.sh linux-source output-directory}")
output_dir=$(realpath -m -- "${2:?Specify a separate output directory}")
profile=${3:-probe}
case "$profile" in probe|live) ;; *) printf 'Profile must be probe or live.\n' >&2;exit 1;; esac
test -f "$source_dir/Makefile"
for tool in clang ld.lld llvm-nm llvm-objcopy llvm-readelf; do command -v "$tool" > /dev/null; done
grep -qx 'VERSION = 6' "$source_dir/Makefile"
grep -qx 'PATCHLEVEL = 12' "$source_dir/Makefile"
install -m 0644 "$project/targets/samsung/gta9wifi/linux/kblfb.c" "$source_dir/drivers/tty/serial/kblfb.c"
if ! grep -q '^obj-$(CONFIG_SERIAL_EARLYCON) += kblfb.o$' "$source_dir/drivers/tty/serial/Makefile"; then
    printf '\nobj-$(CONFIG_SERIAL_EARLYCON) += kblfb.o\n' >> "$source_dir/drivers/tty/serial/Makefile"
fi
mkdir -p "$output_dir"
make -C "$source_dir" O="$output_dir" ARCH=arm64 LLVM=1 allnoconfig > "$output_dir/configure.log"
"$source_dir/scripts/kconfig/merge_config.sh" -m -O "$output_dir" "$output_dir/.config" \
    "$project/targets/samsung/gta9wifi/linux/probe.config" >> "$output_dir/configure.log"
if [ "$profile" = live ]; then
    "$source_dir/scripts/kconfig/merge_config.sh" -m -O "$output_dir" "$output_dir/.config" \
        "$project/targets/samsung/gta9wifi/linux/live.config" >> "$output_dir/configure.log"
fi
make -C "$source_dir" O="$output_dir" ARCH=arm64 LLVM=1 olddefconfig >> "$output_dir/configure.log"
for option in ARM64 ARM_GIC_V3 ARM_PSCI_FW ARM_ARCH_TIMER OF SERIAL_EARLYCON FONT_8x16 BLK_DEV_INITRD BINFMT_ELF; do
    grep -qx "CONFIG_$option=y" "$output_dir/.config"
done
if [ "$profile" = live ]; then
    for option in BINFMT_SCRIPT BLOCK BLK_DEV_LOOP EXT4_FS INPUT_EVDEV USB USB_GADGET NET UNIX INET DRM DRM_SIMPLEDRM FRAMEBUFFER_CONSOLE; do
        grep -qx "CONFIG_$option=y" "$output_dir/.config"
    done
fi
make -C "$source_dir" O="$output_dir" ARCH=arm64 LLVM=1 -j"${KBL_BUILD_JOBS:-8}" Image > "$output_dir/build.log" 2>&1
sha256sum "$output_dir/arch/arm64/boot/Image"
printf 'KBL upstream Linux %s kernel built\n' "$profile"
