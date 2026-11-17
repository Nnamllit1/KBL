# Architecture

The intended SM-X110 boot chain is:

```text
Samsung bootloader
  ├─ normal boot → device UEFI stage → KBL → Android / Linux / another EFI loader
  ├─ recovery    → existing TWRP and its own kernel
  └─ download   → Samsung Download Mode
```

The current SM-X110 implementation uses the original Linux-compatible ARM64
Image as a carrier. Its entry branch runs C++ before the kernel, draws through
the inherited framebuffer, restores the incoming state and returns to the
original kernel for Android startup. It supplies no UEFI services. Its source
is `platforms/baremetal`, with drawing/discovery in the SM-X110 target. See
[the display bridge](display-bridge.md).

An optional embedded Linux entry uses a distinct kernel and initramfs. The
loader validates its memory span, prepares a separate DT and commits a direct
ARM64 kernel handoff. The original Android path remains the default. This does
not supply UEFI services or load files from tablet storage; see
[the Linux probe](linux-probe.md).

The device UEFI stage remains a porting task. The UEFI executable implements
KBL's menu and payload loading after UEFI services are available.

## Portable core

`include/kbl` and `src` contain the menu state machine, bounded configuration
parser, Android boot-header parser, ARM64/PE inspection and flattened device-tree
reader. The core has no operating-system calls, heap allocation, exceptions,
RTTI or standard C++ library dependency. The same sources build for the host
tools and the freestanding firmware application.

Boot headers v0–v2 carry hardware data within the image. Headers v3/v4 split
that data into `vendor_boot`; its fragment table is validated independently.
Recovery and normal BOOT can use different header generations on one device.

Device trees are bounded, validated views into an existing buffer. Android DT
tables are containers: a selected entry must be extracted before it can be
passed to Linux as an FDT. The live bootloader-supplied tree is distinct from
the packaged vendor tree.

## Firmware adapters

`platforms/uefi` uses standard console input/output, filesystem, image-loading
and reset services. It loads only payloads matching the current CPU architecture.
Linux initrds are supplied through the Linux LoadFile2 protocol. A returning
payload is unloaded; temporary initrd protocols and allocations are removed.

The host adapter provides inspection and a menu preview. It never executes boot
actions on the development PC.

## Device support

`targets/<vendor>/<device>` holds the target profile and port requirements.
A board firmware stage must provide usable RAM mappings, display, buttons,
storage, clocks/timers and an appropriate device tree or ACPI interface.
Those responsibilities are separate from menu policy.

Android needs its original kernel, ramdisk, hardware description and dynamic
boot arguments. Later multiboot support also needs a deliberate partition and
data-isolation design. Replacing a kernel path alone is insufficient.

For the SM-X110, the existing Samsung bootloader and recovery partition remain
outside the proposed normal-boot payload. Recovery remains an alternative path
if normal BOOT fails, provided its image, shared verification state and Samsung
loader remain valid. This is a recovery design, not a guarantee against every
possible firmware failure.

## Configuration

Configuration is printable ASCII, at most 32 KiB, with up to 12 entries:

```text
default=linux
timeout=5
entry=linux|Linux|efi|/EFI/Linux/Image.efi|console=ttyAMA0|/EFI/Linux/initramfs.cpio
entry=off|Power off|poweroff|||
```

The six entry fields are ID, label, action, absolute image path, arguments and
absolute initrd path. There is no quoting or pipe escaping. The actions are
`efi`, `linux`, `poweroff`, `restart`, `exit`, `android`, `recovery` and `download`.
The last three require a device adapter; UEFI alone cannot execute them.
Raw `linux` entries require a device adapter; the current SM-X110 probe embeds
its payload at build time. Both image actions require absolute paths in parsed
configuration. Non-image actions must leave the last
three fields empty. Timeout zero waits indefinitely; automatic selection is
allowed only for an EFI payload or Android. Duplicate IDs, path traversal, oversized values
and unknown keys invalidate the entire configuration.

Payloads must be trusted by the device owner. KBL's header checks prevent
format/architecture mistakes; they do not authenticate operating systems.

