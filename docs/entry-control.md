# Pre-kernel execution control

This diagnostic establishes whether the pre-kernel C++ code ran independently
of display support. It intentionally continues into the original CYE5 Android
kernel. It does not display a KBL screen.

The control preserves the original kernel, with two file changes: an entry
branch to the appended observer and its larger memory-span field. The C++
observer changes two strings **in RAM**, then restores the boot arguments and
branches to the original kernel entry. It needs no stack, FDT parser, display,
timer, exception vectors or device-register writes.

The file still contains the original strings. After successful execution:

- The kernel log begins with `KBL bootstrap` instead of `Linux version`.
- `/proc/version` begins with `Linux KBL_CPP` instead of `Linux version`.

Kernel release, module compatibility information, Android firmware version and
the embedded BOOT rollback fields are unchanged. Only display text is modified
at runtime. The SM-X110 test confirmed the runtime marker and successful normal
Android boot with the CYE5 baseline. This does not validate display support or a
complete UEFI environment.

The optional VM check is `python tools/entry_control_smoke.py --qemu /path/to/qemu-system-aarch64`.
Its fixture stops at the original kernel entry, checks both runtime markers and
boot-argument preservation, and verifies that the observer runs with no stack.
It does not attempt to run the MediaTek kernel on QEMU's unrelated board.

## Build

```sh
python tools/build_entry_control.py --source-boot CYE5-boot.img
python tools/package_probe.py --source-boot CYE5-boot.img --build build-entry-control/gta9wifi --out entry-control-package
```

The source must be the audited original or supported rooted CYE5 BOOT. The
packager verifies that neither marker was inserted into its target string before
execution. It produces a BOOT-only ENTRY_CONTROL AP and exact-input restore AP.

## Test

Flash the ENTRY_CONTROL AP in Odin++'s AP slot only, with other slots empty and
erase/repartition off. After PASS, select Restart Android, acknowledge the
bootloader warning and let normal Android start. A KBL screen is not
expected from this control.

Once normal Android has ADB:

```sh
python tools/check_entry_control.py --adb /path/to/adb --serial DEVICE_SERIAL
```

No Android root grant is required. A positive marker proves that the C++
observer executed and handed control to the original kernel. Reading a separate
recovery kernel does not test the normal BOOT image.

If Android does not start, use the supplied BOOT-only restore in AP. This
control does not provide a guaranteed persistent trace of a failure before
the original kernel runs; external debugging or verified retained-memory logging
may still be needed for that case.
