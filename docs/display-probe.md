# SM-X110 bare-metal display probe

**The initial 0.1.0 candidate bootloops on hardware and is withdrawn from the
current test folder.** The next diagnostic is the [pre-kernel entry control](entry-control.md).

The first device candidate is deliberately small: a KBL banner, colour
patches and a blinking square. It is freestanding ARM64 assembly and C++ with
its own stack and exception vectors. There is no Linux kernel, Android init,
UEFI dependency, filesystem access or USB/ADB service in the running probe.

This is an experimental SM-X110/CYE5 image. The code executes and renders in
ARM64 VM tests; physical display operation remains unresolved. An unchanged
Samsung logo, blank screen or reset does not prove whether its entry point ran.

## Build and package

Use Python 3.11+. Install LLVM with `clang++`, `ld.lld`, `llvm-nm` and `llvm-objcopy`, or an Android
NDK. The build script discovers an installed NDK; `LLVM_BIN` or `--llvm-bin`
can select another toolchain.

```sh
python tools/build_probe.py
python tools/package_probe.py --source-boot CYE5-boot.img --out probe-package
```

The packager accepts the audited original CYE5 BOOT or the supported rooted
test image. Inputs are read only. It preserves the generic ramdisk, zero-filled
GKI signature slot, original Samsung metadata, embedded AVB block and OS field.
The modified kernel is not authenticated by the retained original signatures;
the already-unlocked, modified-verification test setup is required.

Output:

```text
probe-package/
  probe/AP_KBL_SM-X110_CYE5_DISPLAY_PROBE_0.1.0.tar.md5
  restore/AP_RESTORE_SM-X110_CYE5_PRE_KBL_BOOT_ONLY_0.1.0.tar.md5
  manifest.json
```

Each AP contains exactly one `boot.img`. The restore AP reproduces the input
BOOT byte for byte. Keep it accessible before starting the test.

## Flash the probe

1. From the working TWRP, choose **Reboot → Download**.
2. Select the **DISPLAY_PROBE** archive in Odin++'s **AP** slot. Leave other
   slots empty, with erase and repartition off.
3. After PASS, choose **Restart Android** to select normal BOOT. Acknowledge the
   unlocked-bootloader warning with Power if requested.
4. Look for **KBL / BARE METAL C++**, three colour patches and a blinking
   square near the upper-right corner.

The probe stays on that screen. It has no menu, shutdown control or ADB. Seeing
that screen is the complete success condition for this first candidate.

## Restore normal Android

Return to Download Mode with the hardware buttons and flash the **PRE_KBL
BOOT_ONLY** restore archive in AP, with the other slots empty and erase and
repartition off. After PASS, choose Restart Android. This restores the exact
previous BOOT; no full AP, CSC or data wipe is part of this restore procedure.

Neither package contains BL, recovery, vendor_boot, DTBO, vbmeta partitions,
secure firmware, super, userdata, metadata, EFS, CSC or PIT. The display probe
does not write storage while running. Existing TWRP and Samsung's loader remain
outside the modified partition.

File audits verify unchanged embedded version/rollback fields. They do not
measure hardware rollback counters or guarantee the outcome of a physical flash.

## Implementation

`platforms/baremetal` implements entry, relocation, cache maintenance and the
idle loop. `targets/samsung/gta9wifi/display_probe.cpp` discovers the inherited
framebuffer from the live FDT and draws within its reservation. The framebuffer
address is not copied from a previous boot. The initial target assumes 800×1340,
32-bit pixels, 800-pixel stride and 1344 stored rows per buffer; these layout
assumptions are part of the first hardware test.

The target only reloads an existing MT6789 watchdog when its FDT node matches
the expected controller. It does not change watchdog configuration or reboot
reason registers. The probe has no partition-writing driver.

For VM tests, `build_probe.py --qemu` builds a separate image with PL011 tracing:

```sh
python tools/build_probe.py --qemu
python tools/probe_smoke.py --qemu /path/to/qemu-system-aarch64
```

The test enters at EL1/EL2, reads back framebuffer memory, checks buffer bounds
and verifies the blinking indicator. Test builds are rejected by the tablet
packager.

The entry format follows the [ARM64 Image boot contract](https://docs.kernel.org/arch/arm64/booting.html).
The watchdog reload register is described in the [MediaTek watchdog driver](https://github.com/torvalds/linux/blob/master/drivers/watchdog/mtk_wdt.c).
