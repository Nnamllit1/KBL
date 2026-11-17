# Upstream Linux desktop bring-up

The Linux entry uses an independent upstream kernel. Device support belongs in
small, separate patches; vendor source can provide a hardware reference or
licensed driver code where useful. Android remains the default KBL menu entry.

The original Linux 6.12 probe boots on SM-X110 with visible kernel text and a
small PID 1. The live-system profile extends that base with BusyBox, standard
filesystem/input/USB/network frameworks and an inherited-framebuffer display
path. A graphical desktop is not working yet.

## Display and progress

The existing `kblfb` early console displays kernel messages immediately after
Linux starts. KBL's Linux-only device tree also describes a `simple-framebuffer`
using the loader's existing address, dimensions, stride and pixel format.
Upstream `simpledrm` and fbcon can then provide a standard DRM device and continue
console output without a complete MediaTek panel/GPU driver.

The diagnostic live entry retains `kblfb` with `keep_bootcon` during this
transition. It mirrors kernel messages to all three inherited display buffers;
fbcon alone updates the base buffer. This keeps logs visible while the active
scanout buffer is still being investigated. The early console wraps around the
screen, so the newest line is not always at the bottom.

The framebuffer reservation remains excluded from general RAM allocation. This
path depends on firmware leaving display scanout running; it does not establish
power management, backlight, accelerated rendering or a full panel driver.
Unused clocks and power domains are retained during early bring-up.

## Build the live-system foundation

Use the upstream kernel source and dependencies documented in the
[Linux probe guide](linux-probe.md). Inside an isolated Linux builder:

```sh
bash tools/build_linux_kernel.sh /path/to/linux-6.12 /build/linux-live live
```

On the packaging host, provide a separately obtained static ARM64 BusyBox and
an audited CYE5 BOOT input:

```sh
python tools/build_linux_live_init.py --busybox /path/to/arm64/busybox --out live-init
python tools/build_entry_control.py --boot-menu --source-boot CYE5-boot.img --linux-image /build/linux-live/arch/arm64/boot/Image --linux-initrd live-init/initramfs.cpio.gz --out build-live/gta9wifi
python tools/package_probe.py --source-boot CYE5-boot.img --build build-live/gta9wifi --out live-package
```

The Linux selection transfers control to the upstream Image. The live initramfs
contains no vendor kernel modules and does not mount Android filesystems. It
prints `KBL_UPSTREAM_LIVE_INIT_OK` and enumerates available display/input/USB
interfaces. Compiling a framework does not establish a working hardware driver.

The live-system VM check also verifies userspace text in every display buffer:

```sh
python tools/linux_smoke.py --qemu /path/to/qemu-system-aarch64 --build build-live/menu-test --kernel-system-map /build/linux-live/System.map --out live-vm
```

Build that VM fixture with the same arguments as the tablet stage plus
`--menu-test`. Tablet packaging rejects VM fixtures.

Flash the generated `LINUX_LIVE` AP in AP only, with other slots empty and
erase/repartition off. First verify normal Android startup. Then restart and
select **Linux live system (upstream)**. Kernel text should replace KBL's status
screen. Record the last visible line if startup stops. A forced restart returns
to the default Android countdown. The separate restore AP restores the exact
BOOT input. Package checks do not certify hardware rollback counters.

## Remaining hardware work

The first desktop milestone requires a working upstream display, touchscreen,
USB input and a root filesystem delivery/storage path. The tested tablet uses
a Novatek touchscreen and UFS storage. These need their own driver and device
tree work. Networking and Wi-Fi follow the basic desktop.

The prepared Debian ARM64 userspace includes Weston with software rendering and
Firefox. Its archive can be kept separately from BOOT, but cache storage is
only useful when the upstream storage driver can read it. The live initramfs
does not currently load that archive. Persistent desktop storage comes later.

The earlier Samsung-kernel desktop candidate was withdrawn after failing its
device boot test. The active path uses upstream Linux. Its build tools remain
available for historical experiments; they are not the desktop port's base.

## Graphics and input

Software rendering is the first target. GPU acceleration, Steam, game
compatibility and performance need separate validation after the desktop works.
Windows bring-up follows a usable Linux system.

Touch drivers should report standard Linux input events. A later userspace
mapper can offer direct touch on the desktop/browser and relative mouse input
for games, with a manual override. That mapper is not implemented yet.
