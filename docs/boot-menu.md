# SM-X110 hardware boot menu

KBL 0.2.0 offers Android, optional embedded Linux, TWRP Recovery and Download Mode in a monochrome
terminal-style menu using antialiased JetBrains Mono. Navigation and Android startup were confirmed on the
SM-X110. Recovery and Download Mode currently return to KBL on the tablet and
remain experimental. The [external kernel payload](payloads.md) is a separate direct
kernel handoff.

Volume Up/Down changes the selection. Press and release Power to start it.
Android starts after eight seconds without input. Pressing a button pauses that
countdown. Buttons held through Samsung's boot warning are ignored until they
have been released. A 90-second limit returns to Android if the menu is left
idle after input.

Android uses the original kernel handoff. Recovery and Download Mode request a
Samsung firmware reboot using its retained SRAM restart markers and the
PSCI SYSTEM_RESET service through the device tree's SMC conduit. The watchdog
is stopped before that call, as on the stock kernel's reboot path. Recovery also sets the device tree's recovery mode;
Download Mode follows Samsung's `download` command and leaves the generic
syscon mode unchanged. These entries do not read OS images from storage. A
reset that returns instead of restarting restores the saved SRAM words and
mode registers, records the firmware status and then continues to Android. No
direct watchdog reset is attempted on a returned firmware call. TWRP and the Samsung loader are
retained.

The port validates the SM-X110 keypad mapping, MT6366 key layout, MT6789 PWRAP
register range, watchdog modes and PSCI SMC method before polling. PMIC transactions read only
key status; the driver does not change PMIC configuration or GPIO setup. A
missing layout, timed-out input transaction or caught current-level exception
falls back to the original kernel. This cannot recover from every possible
hardware or higher-firmware fault.

## Build and flash

```sh
python tools/build_entry_control.py --boot-menu --source-boot CYE5-boot.img
python tools/package_probe.py --source-boot CYE5-boot.img --build build-boot-menu/gta9wifi --out boot-menu-package
```

Use an audited original or supported rooted CYE5 BOOT image and an already
unlocked SM-X110 on X110XXU4CYE5. Flash the BOOT_MENU AP in AP only, other slots
empty, erase/repartition off. After PASS, restart Android and acknowledge the
Samsung warning. First let the eight-second timeout start Android before
testing the two reboot routes. Touch input is not implemented in this stage.

The archive contains only `boot.img`. It retains the original ramdisk, Samsung
metadata, embedded AVB block and BOOT OS/rollback fields. It does not contain
BL, recovery, vendor BOOT, root vbmeta, PIT or userdata. File checks do not
certify hardware rollback counters. The modified image requires the existing
unlocked verification setup; retained signatures do not authenticate its code.

If needed, flash the separate exact-input BOOT restore in AP only. No CSC or
wipe is required for this BOOT-only restore.

## Validation

Run native tests with `tools/build.ps1` or CMake/CTest. Validate a locally
captured device tree with `kbl check-controls live.dtb`. The latter reads a file
and performs no device operations.

The VM fixture stops at original kernel entry and emulates the key-register
bus; it does not run the Samsung kernel or validate physical PMIC access.

```sh
python tools/build_entry_control.py --boot-menu --menu-test --source-boot CYE5-boot.img
python tools/display_bridge_smoke.py --qemu /path/to/qemu-system-aarch64 --build build-boot-menu/menu-test --menu-case timeout
```

Other cases are `android`, `recovery`, `download`, `busy` and `carried`.
VM-bus and fault-injection images are rejected by the tablet packager.
`--boot-menu --menu-test --reset-fault-test` also injects an exception after
reboot-mode preparation to verify fault-handler restoration before handoff.

The compiled SMC wrapper can also be checked against a VM-only EL3 monitor:

```sh
python tools/psci_smoke.py --qemu /path/to/qemu-system-aarch64 --build build-boot-menu/gta9wifi
```

This verifies the actual instruction, zeroed arguments, signed error result and
register/stack restoration at EL1 and EL2. The test monitor returns an error;
it does not reset the VM or establish that a tablet will enter the requested mode.

Register references: [MT6789 PWRAP layout](https://github.com/linux-mediatek/edk2-mtk/blob/next/Silicon/MediaTek/MT6789Pkg/Library/PmicWrapperImplLib/PmicWrapperImplLib.c),
[Linux PMIC wrapper protocol](https://github.com/torvalds/linux/blob/master/drivers/soc/mediatek/mtk-pmic-wrap.c),
[PMIC key status](https://github.com/torvalds/linux/blob/master/drivers/input/keyboard/mtk-pmic-keys.c),
[keypad status](https://github.com/torvalds/linux/blob/master/drivers/input/keyboard/mt6779-keypad.c)
and [watchdog control](https://github.com/torvalds/linux/blob/master/drivers/watchdog/mtk_wdt.c).
Board-specific addresses and button mappings come from the SM-X110 device tree.
Samsung's additional restart contract is described in
[sec_reboot.c](https://github.com/prabhatkrmishra/kernel_samsung_gta9/blob/15.0/drivers/samsung/sec_reboot.c),
[mboot_params.c](https://github.com/prabhatkrmishra/kernel_samsung_gta9/blob/15.0/drivers/misc/mediatek/aee/mrdump/mboot_params.c)
and [sec_debug.h](https://github.com/prabhatkrmishra/kernel_samsung_gta9/blob/15.0/include/linux/sec_debug.h).
The SRAM address, field offsets and mode values were also checked against the
reference CYE5 `mrdump.ko` and `sec_reboot.ko` binaries.
The stock restart notifier order is Samsung markers, then
[PSCI](https://github.com/prabhatkrmishra/kernel_samsung_gta9/blob/15.0/drivers/firmware/psci/psci.c),
with the [MediaTek watchdog restart](https://github.com/prabhatkrmishra/kernel_samsung_gta9/blob/15.0/drivers/watchdog/mtk_wdt.c)
as a lower-priority fallback. KBL uses the standard SYSTEM_RESET function rather
than bypassing PSCI with a watchdog software trigger.

## Appearance

The portable text-cell layout is separate from framebuffer painting and boot
policy. A `MenuTheme` supplies colours and font scale. The renderer caches its
previous frame, writes only changed pixels and returns dirty rectangles for
cache maintenance. The screen is cleared on the initial draw or theme change.
Unchanged frames require no framebuffer writes.
Menu outlines and selection backgrounds use pixel geometry, rather than
character-art borders.

JetBrains Mono Regular is rasterized into a compact 4-bit coverage atlas with
proper lowercase and antialiasing. The font rasterizer runs only when generating
the checked-in asset; the boot stage decodes the baked glyphs directly in C++.
See [font source, license and regeneration](../assets/fonts/jetbrains-mono/README.md).

This is the foundation for later theme files and backgrounds. External style
files, CSS syntax and image loading are not implemented yet.
