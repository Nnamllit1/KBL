# Instrumented pre-kernel display bridge

This candidate extends the device-tested entry control. It attempts to draw a
KBL screen, then continues into the original Android kernel. Its status
is recorded in the kernel's `/proc/version` text, readable without root.

The SM-X110/CYE5 hardware test confirmed a visible KBL screen, completed
drawing without a caught fault, and successful Android boot afterward. The
installed BOOT hash matched the tested package; recovery, vendor BOOT and root
vbmeta matched the baseline. This validates the initial display bridge, not a
complete boot menu or UEFI device port.

Expected result: a brief KBL screen with colour patches and **STARTING
ANDROID**, followed by normal Android. A missing visible screen is still useful
if Android starts: the status distinguishes validation failure, completed
framebuffer writes and a caught synchronous exception.

The bridge uses its own 64 KiB stack after the original kernel's memory span.
It saves the incoming boot arguments, stack banks/selection, interrupt masks and
exception-vector address. For supported entry levels with the MMU off, it
temporarily installs exception vectors around the display attempt. A caught
current-level synchronous exception records ESR/ELR/FAR, restores pre-exception
PSTATE through exception return, and resumes the original kernel.

This fallback is not a guarantee against all hardware faults. Exceptions routed
to higher firmware, memory corruption, watchdog expiry or failures outside the
instrumented section can still prevent Android from starting.

Bare-metal code is built with strict alignment. Device-tree properties can be
4-byte aligned even when they contain 64-bit values; the compiler must not
replace byte decoding with an unaligned 64-bit access before memory setup.
The VM fixture uses this layout and enables alignment checking.

## Build

```sh
python tools/build_entry_control.py --display-bridge --source-boot CYE5-boot.img
python tools/package_probe.py --source-boot CYE5-boot.img --build build-display-bridge/gta9wifi --out display-bridge-package
```

The AP writes BOOT only. The exact-input BOOT restore is included separately.
Kernel release/module compatibility, generic ramdisk, original Samsung metadata,
embedded AVB block and BOOT OS/rollback fields are retained. This remains an
experimental image for the already-unlocked CYE5 test configuration.

## Test and read status

Enter Download Mode, flash the DISPLAY_BRIDGE AP in AP only, leave other slots
empty and erase/repartition off. After PASS, choose Restart Android and
acknowledge the bootloader warning. Leave Android connected after it starts.

```sh
python tools/check_entry_control.py --adb /path/to/adb --serial DEVICE_SERIAL
```

`framebuffer_write_completed` means the drawing and cache-clean steps returned.
It does not establish that the display controller scanned out that memory.
The trace includes stage, exception level, SCTLR, FDT/framebuffer addresses and
fault details. `Linux KBL_E1M` or `Linux KBL_E2M` means an incoming active MMU was
detected and the display attempt was skipped rather than guessing its mapping.

If Android fails to start, use the separate BOOT-only restore. Reading TWRP's
separate kernel does not report this BOOT image's current trace.

## VM validation

```sh
python tools/display_bridge_smoke.py --qemu /path/to/qemu-system-aarch64
python tools/display_bridge_smoke.py --qemu /path/to/qemu-system-aarch64 --bad-fdt
python tools/build_entry_control.py --display-bridge --fault-test --source-boot CYE5-boot.img
python tools/display_bridge_smoke.py --qemu /path/to/qemu-system-aarch64 --build build-display-bridge/fault-test
```

The test fixture stops at the original kernel entry. It validates drawing,
fault/rejection return, runtime markers and saved-state restoration at EL1/EL2;
it does not run the MediaTek Android kernel on QEMU's virtual board. Fault-test
images are rejected by the tablet packager.
