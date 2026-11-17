# External payloads and hardware configuration

KBL is a boot manager. Kernels, initramfs images, distributions and their drivers
are separate projects, supplied explicitly as inputs. A normal KBL build has no
Linux system source or build dependency. The UEFI adapter can chainload EFI
applications, including an OS loader; the SM-X110 hardware stage currently
supports its original Android handoff and one embedded ARM64 kernel payload.
Windows boot on SM-X110 is not implemented.

## Hardware menu configuration

The hardware stage uses the same bounded `kbl.cfg` parser as the UEFI adapter.
Choose entry labels, order and timeout in a text file:

```ini
default=android
timeout=8
entry=android|Android|android|||
entry=recovery|TWRP Recovery (experimental)|recovery|||
entry=download|Download Mode (experimental)|download|||
```

Validate and embed it:

```sh
kbl check-hardware-config hardware.cfg
python tools/build_entry_control.py --boot-menu --source-boot CYE5-boot.img --menu-config hardware.cfg --out build-menu
```

Android must remain the default and fallback. Unsupported actions, unavailable
file paths and malformed configurations are rejected. An invalid configuration
encountered by the stage returns to Android. The prototype's 90-second safety
timeout still returns to Android even if a longer menu timeout is configured.

**Storage differs between adapters.** UEFI reads `/kbl.cfg` using firmware
filesystem services. The SM-X110 embedded stage has no UFS, partition or
filesystem driver: its configuration is included in the image at build time.
Changing a file on Android storage does not update that configuration. Rebuild
and package the BOOT-only stage to apply changes. Runtime editing without an
image update needs a future storage adapter; no storage-write support is
claimed by this configuration feature.

## External ARM64 kernel

Add one entry naming the preloaded payload slots. These paths identify embedded
resources; the hardware stage does not open files at those paths:

```ini
entry=linux|My Linux system|linux|/embedded/Image|console=tty0 rdinit=/init|/embedded/initramfs
```

```sh
kbl check-hardware-config hardware.cfg --linux-embedded
python tools/build_entry_control.py --boot-menu --source-boot CYE5-boot.img --menu-config hardware.cfg --linux-image /path/to/Image --linux-initrd /path/to/initramfs.cpio.gz --out build-system
python tools/package_probe.py --source-boot CYE5-boot.img --build build-system --out packaged-system
```

The kernel's arguments come from its config entry. KBL checks the full kernel
memory span, reserves its own still-executing memory and creates a separate
Linux device tree without modifying Android's input tree. The optional
`simple-framebuffer` description uses the inherited display geometry. The
selected OS owns its device drivers and userspace after the handoff.

An OS project can provide a freestanding C++ adapter implementing the interface
in `include/kbl/linux_adapter.hpp`. Pass `--linux-adapter /path/to/adapter.cpp`,
repeating it for dependency sources. Adapters run only on the selected Linux
path and may validate the incoming tree and customize its Linux-only copy.
Adapter sources are executable build inputs; KBL records their hashes but does
not fetch or bundle them. Build without these options to produce KBL alone.

Use `--menu-test` for VM fixtures and `--linux-vm-only` for a test-only OS
payload. Tablet packaging rejects either marker. VM execution does not prove
that an external OS's device drivers work on hardware.
