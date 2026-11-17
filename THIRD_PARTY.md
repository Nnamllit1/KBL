# License scope and external inputs

KBL's core, firmware adapters, host tools, tests and build scripts are MIT
licensed unless a file carries another license notice.

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
