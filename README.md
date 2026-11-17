# KBL (Kernel Bootloader)

A C++ boot manager for mobile devices, with a portable core and separate firmware
and device adapters. The first hardware target is the Samsung Galaxy Tab A9
Wi-Fi (SM-X110 / gta9wifi).

The SM-X110 stage runs before the original Android kernel. Entry, display and
return to Android have been confirmed on the tablet. Its terminal-style menu
offers Android, TWRP Recovery and Download Mode. The two reset entries currently
return to KBL on the tablet and remain experimental.
The [Linux probe](docs/linux-probe.md) adds an embedded upstream kernel and its
own initramfs. Linux boot, framebuffer text, PID 1 and its heartbeat have been
confirmed on the SM-X110. The experimental [Linux desktop](docs/linux-desktop.md)
uses the matched Samsung kernel with Debian ARM64 userspace, Weston and Firefox.
Native offscreen browser rendering works; the standalone desktop still needs
display and input validation.

The UEFI adapter provides a boot menu,
EFI chainloading, Linux kernel arguments and initrd delivery. It does not require
a Linux userspace or C++ runtime to run.

| Component | Status |
| --- | --- |
| Pre-kernel C++ execution control | Confirmed on SM-X110; original Android boots |
| Display bridge with return-to-Android diagnostics | Confirmed visible on SM-X110; Android handoff works |
| SM-X110 menu and Android countdown | Confirmed on SM-X110 |
| Recovery / Download Mode requests | Currently return to KBL on SM-X110 |
| Independent Linux kernel and initramfs | Boots on SM-X110; framebuffer logs, PID 1 and heartbeat confirmed |
| Debian ARM64 browser rendering | Tested natively offscreen on the tablet |
| Standalone desktop, USB peripherals and touchscreen | Experimental; hardware validation pending |
| Terminal UI and incremental redraw | Native/VM tested; hardware redraw check pending |
| Portable menu, configuration and timeout | Implemented |
| ARM64 UEFI adapter | Tested in QEMU |
| EFI chainloading and return to menu | Tested in QEMU |
| Linux kernel + initrd + userspace boot | Tested in ARM64 QEMU |
| Android boot v0–v4, vendor boot v3/v4 and device-tree inspection | Implemented |
| SM-X110 hardware audit and payload preparation | Implemented |
| SM-X110 UEFI firmware stage | Not implemented |
| SM-X110 original Android handoff | Confirmed on SM-X110 |
| Persistent OS installation and shared-data management | Not implemented |
| Windows boot | Not implemented |

The UEFI executable needs a working UEFI environment and cannot be flashed
directly into Samsung's BOOT partition. The SM-X110 stage is packaged for BOOT;
see [the hardware menu guide](docs/boot-menu.md) before testing it.

## Build the SM-X110 stage

The [display bridge](docs/display-bridge.md) provides the tested entry/display
foundation. The [hardware menu](docs/boot-menu.md) extends it with button input.

With Python 3.11+ and LLVM (an installed Android NDK is supported):

```sh
python tools/build_entry_control.py --boot-menu --source-boot CYE5-boot.img
python tools/package_probe.py --source-boot CYE5-boot.img --build build-boot-menu/gta9wifi --out boot-menu-package
```

Supply an audited original or supported rooted CYE5 BOOT image. The output
contains a BOOT-only menu AP and a separate AP restoring the exact input image.
No Linux build VM is required. Source and flash/restore instructions are in
[the hardware menu guide](docs/boot-menu.md).

## Build

Build the host inspector and core tests with CMake and a C++17 compiler:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Windows, `tools/build.ps1` also locates CMake from Visual Studio.

Build the freestanding firmware application with Python 3 and LLVM:

```sh
python tools/build_uefi.py
```

The output is `build-uefi/arm64/BOOTAA64.EFI`. Set `LLVM_BIN`, pass
`--llvm-bin`, or use an installed Android NDK. An x64 build is also available
with `--arch x64`; hardware validation focuses on ARM64.

Copy the executable to `EFI/BOOT/BOOTAA64.EFI` on a FAT filesystem and place
`kbl.cfg` at its root. See [the example configuration](examples/kbl.cfg).
Only entries present in that configuration appear in the menu. Arrow keys select
an entry; Enter starts it. Any other key stops automatic boot. A failed payload
returns to the menu without automatic retry.

For an automated ARM64 VM test:

```sh
python tools/build_uefi.py --trace
python tools/vm_smoke.py --qemu /path/to/qemu-system-aarch64 --firmware /path/to/edk2-aarch64-code.fd
```

This downloads an Alpine Linux ARM64 test kernel, checks failed-entry recovery
and EFI chainloading, boots a small initramfs and verifies Linux powers off.
Pass `--kernel` to use an existing test kernel. Download and build hashes are
recorded locally. Test kernels and firmware are not part of KBL's source.

## Device ports

The [SM-X110 target](targets/samsung/gta9wifi/README.md) describes the initial
port and its remaining requirements. See [the architecture](docs/architecture.md)
for the division between the core, firmware adapter and board support.

The host tools inspect Android boot images and prepare EFI-stub payloads. They
do not flash, unlock, repartition or change rollback state. Prepared Android
payloads still need a device port that preserves the original boot context.

KBL's core, adapters and tools are licensed under [MIT](LICENSE), unless a file
states otherwise. The Linux early-console driver is GPL-2.0-only. Firmware,
kernels and other payloads retain their own licenses. See [third-party notices](THIRD_PARTY.md).
The embedded [JetBrains Mono font](assets/fonts/jetbrains-mono/README.md) is
licensed under the SIL Open Font License 1.1.

