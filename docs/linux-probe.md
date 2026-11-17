# Independent Linux boot probe

KBL 0.2.0 can embed a separate ARM64 Linux Image and initramfs alongside the
original Android kernel. Android remains the default. Selecting **Linux boot
probe** copies the independent kernel into its load region and transfers control
using the ARM64 Linux boot protocol. No EFI services are provided by this stage.

The first probe uses upstream Linux 6.12 with a small C++ PID 1. Its `kblfb`
early console writes kernel messages through the inherited SM-X110 framebuffer.
Reaching userspace prints `KBL_LINUX_INIT_OK`, the kernel release and a periodic
heartbeat. These messages come from Linux, after the handoff.

| Function | Status |
| --- | --- |
| Independent Linux kernel and PID 1 | Confirmed on SM-X110 and in ARM64 QEMU at EL1 and EL2 |
| Early framebuffer text and userspace heartbeat | Confirmed on SM-X110 |
| Original live DT preserved | Checked with VM execution and a captured SM-X110 tree |
| Android default and pre-handoff failure return | Native/VM checked; updated tablet package unverified |
| Linux boot on SM-X110 | Confirmed with the diagnostic kernel and initramfs |
| UEFI services on SM-X110 | Not implemented |
| Storage, USB, networking and touch in this Linux probe | Not implemented |
| Bare-metal touch in KBL | Deferred; requires a controller and bus driver port |

The diagnostic kernel has no block, MMC, MTD or network support. PID 1 runs
entirely from its initramfs and performs no mounts or block-device access. This
is an early boot test, not an installed Linux distribution or a complete device
port. The display still depends on the state inherited from Samsung's loader.
The next stage is a [usable Linux desktop](linux-desktop.md).

## Build the kernel

On a Linux build machine, install a C build environment, Clang, LLD, LLVM tools,
Flex, Bison, `bc`, OpenSSL headers and libelf headers. Use a separate source and
output directory; no kernel installation on the build machine is needed.

The upstream input is
[linux-6.12.tar.xz](https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.tar.xz),
SHA-256 `b1a2562be56e42afb3f8489d4c2a7ac472ac23098f1ef1c1e40da601f54625eb`.
Verify the archive before extracting it.

```sh
bash tools/build_linux_kernel.sh /path/to/linux-6.12 /path/to/linux-output
```

This adds [the GPL-2.0-only early console](../targets/samsung/gta9wifi/linux/kblfb.c)
to that source tree and applies the target's diagnostic configuration. Linux
retains its upstream GPL licensing. Keep the configuration and modified source
available when distributing a compiled kernel.

## Build the BOOT package

From the KBL source directory, with LLVM or the Android NDK installed:

```sh
python tools/build_linux_init.py --out build-linux-init
python tools/build_entry_control.py --boot-menu --source-boot CYE5-boot.img --linux-image /path/to/linux-output/arch/arm64/boot/Image --linux-initrd build-linux-init/initramfs.cpio --out build-linux/gta9wifi
python tools/package_probe.py --source-boot CYE5-boot.img --build build-linux/gta9wifi --out linux-probe-package
```

Use an audited original or supported rooted CYE5 BOOT image. The output contains
one `LINUX_PROBE` AP writing only `boot.img`, plus a separate AP restoring the
exact input BOOT. It requires an already unlocked SM-X110 on X110XXU4CYE5.
Recovery, vendor BOOT, root vbmeta, Samsung bootloader and userdata are excluded.
Retained BOOT OS/rollback fields are file checks, not a hardware-counter guarantee.

## Test

Flash the probe in AP only, other slots empty and erase/repartition off. After
PASS restart Android, acknowledge Samsung's warning and first let the countdown
start Android. On the next boot select **Linux boot probe** using Volume Down,
then press and release Power. No CSC or wipe is needed.

Look for actual kernel text and `KBL_LINUX_INIT_OK`. A KBL status message alone
does not prove Linux executed. If it hangs or panics, record the last visible
message. A forced restart reloads the menu with Android still the default; the
selection is not saved. The probe has no Linux power-button driver or normal
shutdown interface yet. Use the exact-input BOOT restore if normal boot fails.

The loader checks the complete kernel memory span against its own executing
region before copying. It creates a separate device tree with the Linux command
line/initramfs, reserves its payload memory and marks the existing framebuffer
reservation `no-map`. Failed preparation returns to the original Android kernel.
Once the copy is committed, Linux owns the boot; a later Linux failure requires
a restart rather than an automatic return to Android.

## VM verification

Build with the same arguments plus `--menu-test` into a separate directory, then:

```sh
python tools/linux_smoke.py --qemu /path/to/qemu-system-aarch64 --build build-linux/menu-test --out build-linux/vm-test
```

The VM uses QEMU's real platform tree with a synthetic key-register bus and
framebuffer region. It checks that KBL starts a real Linux kernel, that the
independent PID 1 runs, and that the original tree and framebuffer guards remain
intact. QEMU's PSCI method is retained for its Linux kernel. Every VM-bus build
is rejected by the tablet packager. VM success does not establish SM-X110
hardware compatibility.
