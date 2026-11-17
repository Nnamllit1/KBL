#include "kbl/boot_image.hpp"

namespace kbl {
static uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static uint64_t le64(const uint8_t* p) { return uint64_t(le32(p)) | uint64_t(le32(p + 4)) << 32; }
static uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); }
static bool fits(Region r, size_t total) { return r.offset <= total && r.size <= total - r.offset; }
bool inspect_android(Bytes b, AndroidImage& out, const char*& error) {
    error = nullptr;
    auto fail = [&](const char* why) { error = why; return false; };
    if (b.size < 48 || !equal({reinterpret_cast<const char*>(b.data), 8}, text("ANDROID!"))) return fail("not an Android boot image");
    AndroidImage v{};
    v.version = le32(b.data + 40);
    if (v.version > 4) return fail("boot header version is not supported (expected 0..4)");
    if (v.version >= 3) {
        size_t header_size = v.version == 3 ? 1580 : 1584;
        if (b.size < header_size || le32(b.data + 20) != header_size) return fail("invalid v3/v4 boot header size");
        for (size_t i = 24; i < 40; i += 4) if (le32(b.data + i)) return fail("nonzero reserved boot header field");
        v.page_size = 4096;
        v.os_version = le32(b.data + 16);
        uint64_t pos = 4096;
        auto take = [&](Region& region, uint64_t size) {
            region = {pos, size};
            if (!fits(region, b.size)) return false;
            pos += (size + 4095) & ~uint64_t(4095);
            return true;
        };
        if (!take(v.kernel, le32(b.data + 8)) || !take(v.ramdisk, le32(b.data + 12)) ||
            (v.version == 4 && !take(v.signature, le32(b.data + 1580)))) return fail("v3/v4 boot payload is truncated");
        size_t n = 0;
        while (n < 1536 && b.data[44 + n]) { v.command_line.data[n] = char(b.data[44 + n]); ++n; }
        v.command_line.size = n; v.command_line.data[n] = 0;
        out = v;
        return true;
    }
    const uint32_t header_bytes[] = {1632, 1648, 1660};
    if (b.size < header_bytes[v.version]) return fail("truncated boot header");
    v.page_size = le32(b.data + 36);
    if (v.page_size < 2048 || v.page_size > 65536 || (v.page_size & (v.page_size - 1))) return fail("invalid page size");
    if (v.page_size < header_bytes[v.version]) return fail("header exceeds its page");
    if (v.version && le32(b.data + 1644) != header_bytes[v.version]) return fail("unexpected header size");
    v.kernel_address = le32(b.data + 12);
    v.ramdisk_address = le32(b.data + 20);
    v.os_version = le32(b.data + 44);
    uint64_t pos = v.page_size;
    auto take = [&](Region& r, uint64_t size) {
        r = {pos, size};
        if (!fits(r, b.size)) return false;
        pos += (size + v.page_size - 1) & ~uint64_t(v.page_size - 1);
        return true;
    };
    if (!take(v.kernel, le32(b.data + 8)) || !v.kernel.size ||
        !take(v.ramdisk, le32(b.data + 16)) || !take(v.second, le32(b.data + 24))) return fail("boot payload is truncated");
    if (v.version) {
        uint64_t dtbo_size = le32(b.data + 1632), dtbo_offset = le64(b.data + 1636);
        if (dtbo_size) {
            if (dtbo_offset != pos) return fail("recovery DTBO offset is inconsistent with payload layout");
            if (!take(v.recovery_dtbo, dtbo_size)) return fail("recovery DTBO is truncated");
        }
    }
    if (v.version == 2) {
        if (!take(v.dtb, le32(b.data + 1648))) return fail("DTB is truncated");
        v.dtb_address = le64(b.data + 1652);
    }
    // The extra command-line field continues the first field when it is full.
    size_t n = 0;
    for (size_t i = 0; i < 512 && b.data[64 + i]; ++i) v.command_line.data[n++] = char(b.data[64 + i]);
    for (size_t i = 0; i < 1024 && b.data[608 + i]; ++i) v.command_line.data[n++] = char(b.data[608 + i]);
    v.command_line.data[n] = 0;
    v.command_line.size = n;
    out = v;
    return true;
}
bool inspect_vendor(Bytes b, VendorImage& out, const char*& error) {
    error = nullptr;
    auto fail = [&](const char* why) { error = why; return false; };
    if (b.size < 12 || !equal({reinterpret_cast<const char*>(b.data), 8}, text("VNDRBOOT"))) return fail("not a vendor boot image");
    VendorImage v{};
    v.version = le32(b.data + 8);
    if (v.version != 3 && v.version != 4) return fail("vendor boot requires header v3 or v4");
    uint32_t header_size = v.version == 3 ? 2112 : 2128;
    if (b.size < header_size || le32(b.data + 2096) != header_size) return fail("invalid vendor boot header size");
    v.page_size = le32(b.data + 12);
    if (v.page_size < 2048 || v.page_size > 65536 || (v.page_size & (v.page_size - 1))) return fail("invalid vendor page size");
    v.kernel_address = le32(b.data + 16); v.ramdisk_address = le32(b.data + 20); v.dtb_address = le64(b.data + 2104);
    uint64_t pos = (uint64_t(header_size) + v.page_size - 1) & ~uint64_t(v.page_size - 1);
    auto take = [&](Region& r, uint64_t size) {
        r = {pos, size};
        if (!fits(r, b.size)) return false;
        pos += (size + v.page_size - 1) & ~uint64_t(v.page_size - 1);
        return true;
    };
    if (!take(v.ramdisk, le32(b.data + 24)) || !take(v.dtb, le32(b.data + 2100))) return fail("vendor payload is truncated");
    if (v.version == 4) {
        if (!take(v.table, le32(b.data + 2112)) || !take(v.bootconfig, le32(b.data + 2124))) return fail("vendor table/bootconfig is truncated");
        uint64_t count = le32(b.data + 2116), stride = le32(b.data + 2120);
        if (count > 64 || (count && (stride < 108 || count * stride != v.table.size)) || (!count && v.table.size))
            return fail("invalid or oversized vendor ramdisk table");
        v.fragment_count = size_t(count);
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* p = b.data + v.table.offset + i * size_t(stride);
            auto& fragment = v.fragments[i];
            uint64_t size = le32(p), offset = le32(p + 4);
            if (offset > v.ramdisk.size || size > v.ramdisk.size - offset) return fail("vendor fragment exceeds ramdisk");
            fragment.data = {v.ramdisk.offset + offset, size};
            fragment.type = le32(p + 8);
            if (fragment.type > 3) return fail("unknown vendor ramdisk type");
            size_t n = 0;
            while (n < 32 && p[12 + n]) { fragment.name.data[n] = char(p[12 + n]); ++n; }
            fragment.name.size = n; fragment.name.data[n] = 0;
            for (size_t j = 0; j < 16; ++j) fragment.board_id[j] = le32(p + 44 + j * 4);
            for (size_t j = 0; j < i; ++j) {
                Region other = v.fragments[j].data;
                if (size && other.size && fragment.data.offset < other.offset + other.size && other.offset < fragment.data.offset + size)
                    return fail("overlapping vendor ramdisk fragments");
            }
        }
    }
    size_t n = 0;
    while (n < 2048 && b.data[28 + n]) { v.command_line.data[n] = char(b.data[28 + n]); ++n; }
    v.command_line.size = n; v.command_line.data[n] = 0;
    out = v;
    return true;
}
bool inspect_arm64(Bytes b, uint64_t& image_size, const char*& error) {
    error = nullptr;
    if (b.size < 64 || le32(b.data + 56) != 0x644d5241) { error = "not an uncompressed ARM64 Image"; return false; }
    image_size = le64(b.data + 16);
    if (image_size && image_size > b.size) { error = "ARM64 Image is truncated"; return false; }
    return true;
}
bool inspect_efi(Bytes b, uint16_t machine, const char*& error) {
    error = nullptr;
    auto fail = [&](const char* why) { error = why; return false; };
    if (b.size < 64 || b.data[0] != 'M' || b.data[1] != 'Z') return fail("not a PE/COFF executable");
    uint32_t pe = le32(b.data + 60);
    if (pe > b.size || b.size - pe < 94 || le32(b.data + pe) != 0x4550) return fail("invalid PE header");
    if (le16(b.data + pe + 4) != machine) return fail("EFI payload architecture does not match");
    uint16_t optional_size = le16(b.data + pe + 20);
    if (optional_size < 70 || optional_size > b.size - pe - 24) return fail("invalid PE optional header");
    if (le16(b.data + pe + 24) != 0x20b || le16(b.data + pe + 24 + 68) != 10) return fail("expected a 64-bit EFI application");
    return true;
}
} // namespace kbl
