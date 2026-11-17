#pragma once
#include "kbl/core.hpp"

namespace kbl {
struct Bytes { const uint8_t* data = nullptr; size_t size = 0; };
struct Region { uint64_t offset = 0; uint64_t size = 0; };
struct AndroidImage {
    uint32_t version = 0;
    uint32_t page_size = 0;
    uint32_t os_version = 0;
    uint64_t kernel_address = 0;
    uint64_t ramdisk_address = 0;
    uint64_t dtb_address = 0;
    Region kernel, ramdisk, second, recovery_dtbo, dtb, signature;
    String<1537> command_line;
};
// Android boot header versions 0..4. Modern hardware data is in vendor_boot.
bool inspect_android(Bytes bytes, AndroidImage& out, const char*& error);
struct VendorFragment {
    Region data;
    uint32_t type = 0;
    String<33> name;
    uint32_t board_id[16]{};
};
struct VendorImage {
    uint32_t version = 0, page_size = 0;
    uint64_t kernel_address = 0, ramdisk_address = 0, dtb_address = 0;
    Region ramdisk, dtb, table, bootconfig;
    String<2049> command_line;
    VendorFragment fragments[64]{};
    size_t fragment_count = 0;
};
bool inspect_vendor(Bytes bytes, VendorImage& out, const char*& error);
bool inspect_arm64(Bytes bytes, uint64_t& image_size, const char*& error);
bool inspect_efi(Bytes bytes, uint16_t machine, const char*& error);
} // namespace kbl

