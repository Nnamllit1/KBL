# Samsung Galaxy Tab A9 Wi-Fi (SM-X110)

This target is in bring-up. The [pre-kernel display bridge](../../../docs/display-bridge.md)
has displayed a screen and returned to the original Android kernel on the
SM-X110. Hardware menu navigation and Android startup are also confirmed; the
independent [Linux probe](../../../docs/linux-probe.md) also boots with framebuffer
text and a running PID 1. Recovery/Download requests still return to KBL.
A complete UEFI device port remains to be built.

| Property | Reference |
| --- | --- |
| Device | SM-X110 / gta9wifi |
| Platform | MT6789 family; live DT identifies MT8781V/NA |
| Firmware baseline | X110XXU4CYE5 |
| CPU architecture | ARM64 |
| Android BOOT / vendor BOOT headers | Version 4 |
| Separate recovery header | Version 2 |
| BOOT partition | 64 MiB |
| RECOVERY partition | 80 MiB, separate from BOOT |
| Kernel | 5.10.218, gzip-compressed ARM64 Image with EFI stub |
| kexec | Disabled in the reference kernel |

## Read-only audit

With this device connected and either root ADB in recovery or an existing root
grant in Android:

```sh
python tools/device_audit.py --adb /path/to/adb --serial DEVICE_SERIAL --out /private/audit-folder
```

The audit checks the selected profile, kernel options and partition sizes. It
saves the live DTB, kernel configuration and memory map locally. It performs no
device writes or reboots. Live DTBs can contain device identifiers and entropy;
keep audit artifacts out of public source repositories.

The native inspector can validate the live tree:

```sh
kbl dtb /private/audit-folder/live.dtb
kbl inspect boot.img
```

An EFI-stub payload can be prepared from a raw Android BOOT image:

```sh
python tools/prepare_payload.py --boot-image boot.img --vendor-boot vendor_boot.img --inspector /path/to/kbl --out /private/payload
```

Separate `--kernel`, `--ramdisk` and `--dtb` inputs are also supported. A DT table
requires explicit selection with `--dtb-index` when the desired entry is not
zero. The inspector validates the selected FDT before export. These outputs
are payload components, not a complete Android boot solution or Odin package.
The generic and vendor ramdisks remain separate: fragment selection, ordering,
bootconfig and bootloader-provided arguments belong to the device handoff.

## Remaining device work

1. Port an MT6789-family UEFI stage to the SM-X110 entry state. Preserve the live
   Samsung boot context before reclaiming memory.
2. Establish the memory map and reserved regions, framebuffer format/stride,
   button input and storage without changing Samsung's loader or recovery.
3. Boot the original Android kernel and ramdisk through that stage with the
   correct device tree, boot arguments and reserved-memory ownership.
4. Verify repeated Android, TWRP and Download Mode transitions before designing
   persistent multiboot storage.

Related implementations include [edk2-mtk](https://github.com/linux-mediatek/edk2-mtk)
and [Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium). Their board
constants and reported results are not SM-X110 validation. Reusing their
implementation code would require retaining its applicable licenses.

The boot interfaces are described by the [ARM64 boot protocol](https://docs.kernel.org/arch/arm64/booting.html),
[Linux EFI stub documentation](https://docs.kernel.org/admin-guide/efi-stub.html)
and [Devicetree specification](https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html).
