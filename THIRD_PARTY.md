# License scope and external inputs

KBL's core, firmware adapters, host tools, tests and build scripts are MIT
licensed unless a file carries another license notice.

## Linux early console

`targets/samsung/gta9wifi/linux/kblfb.c` is licensed under GPL-2.0-only and is
compiled into a separately built Linux kernel. The complete license is in
[LICENSES/GPL-2.0-only.txt](LICENSES/GPL-2.0-only.txt). The build recipe uses
upstream Linux 6.12; see [the kernel build guide](docs/linux-probe.md).
Linux's existing source and notices retain their upstream licenses.

## JetBrains Mono

The bundled JetBrains Mono Regular font and generated glyph atlas are covered
by the [SIL Open Font License 1.1](assets/fonts/jetbrains-mono/OFL.txt).
The source font, exact version, checksums and regeneration instructions are in
[the font directory](assets/fonts/jetbrains-mono/README.md). The firmware embeds
the font's license notice.

## Firmware and test inputs

Samsung firmware, kernels, modules, device captures, signing material, VM disks,
UEFI firmware and Linux distribution images are not part of this repository.
Build tools require separately obtained firmware inputs and reject unsupported
BOOT images. Those inputs retain their own licenses and redistribution terms.

The optional UEFI VM test uses an Alpine Linux kernel and externally supplied
EDK II firmware. Their provenance is recorded by the local test tool; their
binaries are not included here. KBL implements a UEFI ABI subset using the
published specification and does not bundle a UEFI firmware implementation.

The desktop recipe downloads Debian packages including BusyBox, Weston, Mesa
and Firefox. Their licenses and copyright files remain in the generated root
filesystem under `/usr/share/doc/`; the package/version inventory is emitted
alongside the archive. Distribution images are not committed to KBL.
