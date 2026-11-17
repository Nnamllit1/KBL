# Linux desktop bring-up

KBL 0.3.0 adds an experimental native Debian ARM64 desktop entry using the matched
Samsung 5.10 kernel, Weston with software rendering, and Firefox ESR. Android
remains the default entry. The separate upstream-kernel diagnostic remains
available through the [Linux probe build](linux-probe.md).

Firefox has rendered the local welcome page natively on the tablet without
sandbox overrides, and Weston and Firefox start together offscreen. Standalone
DRM display, touch and USB input still require hardware validation. Networking,
GPU acceleration and Steam have not been demonstrated. Windows work follows a
usable Linux system.

## Hardware foundation

The desktop reuses the CYE5 kernel and matching module ABI instead of depending
on incomplete upstream board support. The display controller is MediaTek DRM;
the tested tablet has a Novatek touchscreen. The recovery port already uses
these drivers, but a desktop compositor has a different display/input path.

The live root filesystem expands into RAM at boot. Its compressed archive and
startup logs live under `/cache/kbl-linux/`. Android userdata is not mounted by
the desktop init scripts, and no new partition or PIT update is required.
Desktop home-directory changes are temporary. Closing the browser ends the
session and requests a normal reboot; early archive/setup errors also request
a normal reboot after saving logs when cache is available.

The CYE5 kernel lacks virtual terminals and devtmpfs, so startup uses BusyBox
coldplug and udev, followed by seatd without VT binding. DRM and input are passed
to an unprivileged desktop user. Firefox's sandbox is not disabled by the
launcher. The stock kernel's namespace limitations still apply.

## Build the experimental desktop

Use an isolated Debian/Ubuntu Linux builder with `mmdebstrap`, `qemu-user-static`,
ARM64 binfmt support, Debian archive keys, `tar` and `xz`. The rootfs builder
requires root inside that builder:

```sh
sudo tools/build_linux_desktop.sh /build/new-kbl-desktop
```

On the packaging host, supply separately prepared SM-X110 CYE5 recovery inputs
(`kernel`, `inputs.json` and `ramdisk/lib/modules` plus its firmware), an audited
CYE5 BOOT image, and the static ARM64 BusyBox from that root filesystem:

```sh
python tools/prepare_linux_desktop_init.py --inputs /private/cye5/prebuilt --source-boot CYE5-boot.img --busybox /build/new-kbl-desktop/rootfs/bin/busybox --rootfs /build/new-kbl-desktop/rootfs.tar.xz --out desktop-init
python tools/build_entry_control.py --boot-menu --linux-reuse-kernel --linux-initrd desktop-init/initramfs.cpio.gz --source-boot CYE5-boot.img --out build-desktop/gta9wifi
python tools/package_probe.py --source-boot CYE5-boot.img --build build-desktop/gta9wifi --out desktop-package
```

The preparation tool validates the pinned kernel, module hashes and vermagic.
It excludes watchdog/hang-monitor modules, embeds the desktop archive's checksum
and records its required filename in `desktop-init/desktop.json`. Firmware and
modules remain external build inputs and are not included in this repository.

Before flashing, copy the archive to the exact `/cache/kbl-linux/` filename in
that manifest using existing rooted Android or recovery access. Check its
checksum and available cache space; the tested archive is about 247 MiB in a
300 MiB cache partition. Do not erase cache to make room. A missing or changed
archive fails verification at Linux startup.

Flash the generated desktop AP in AP only, with other slots empty and
repartition/erase disabled. First verify the default Android entry. Then restart
and select **Linux desktop (experimental)**. Allow time for extraction into RAM.
This userspace currently provides neither ADB nor an SSH server. The separate
restore AP restores the exact BOOT input; it is not a full firmware restore.
No hardware rollback-counter guarantee is established by these package checks.

After returning to Android, startup diagnostics are in
`/cache/kbl-linux/last-kernel.log` and `last-desktop.log`. Recovery and Download
Mode menu reset requests remain experimental and currently return to KBL.

The first desktop milestone covers:

1. A native ARM64 root filesystem and a reliable boot/return path.
2. USB role switching, host-mode power and keyboard/mouse input.
3. Touchscreen events exposed through normal Linux input interfaces.
4. A display compositor using software rendering and a browser showing a page.
5. Networking, beginning with a verified interface, then onboard Wi-Fi.

Persistent desktop storage is a later step, after display and input work.

## Graphics and games

Start with software rendering for the compositor/browser. GPU acceleration is
a later, separately verified layer. Mesa's [Panfrost documentation](https://docs.mesa3d.org/drivers/panfrost.html)
lists Mali-G57 OpenGL support. That does not establish this tablet's display,
power/clock or kernel-driver integration. Vulkan support must also be checked
for the particular GPU; the current Mesa table does not list Vulkan for G57.

Reuse Steam's compatibility stack when the system can support it. Valve's
[Proton changelog](https://github.com/ValveSoftware/Proton/wiki/Changelog)
documents FEX integration for ARM64EC builds. A working browser or software
renderer is not a game-compatibility or performance result. Steam and individual
games need their own tests after graphics and networking work.

## Touch behavior

Keep the kernel driver focused on reporting touch. A userspace input mapper
can provide direct touch on the desktop/browser and relative mouse movement
for games. Linux [uinput](https://www.kernel.org/doc/html/latest/input/uinput.html)
provides the virtual-input interface for this. Per-application switching needs
desktop/compositor integration and a manual override. It is not implemented yet.

USB host mode, touch, Wi-Fi, GPU acceleration and the desktop remain unverified
until their individual device tests pass.
