#include "kbl/boot_image.hpp"
#include "kbl/fdt.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
static unsigned checks = 0;
static void check(bool ok, const char* why) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
static void u32(std::vector<uint8_t>& b, size_t pos, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) b[pos + i] = uint8_t(n >> (i * 8));
}
static std::vector<uint8_t> image(unsigned version) {
    std::vector<uint8_t> b(24576);
    std::memcpy(b.data(), "ANDROID!", 8);
    u32(b, 8, 100); u32(b, 16, 200); u32(b, 36, 4096); u32(b, 40, version);
    if (version) { u32(b, 1632, 30); u32(b, 1636, 12288); u32(b, 1644, version == 2 ? 1660 : 1648); }
    if (version == 2) u32(b, 1648, 40);
    return b;
}
static std::vector<uint8_t> gki(unsigned version) {
    std::vector<uint8_t> b(16384);
    std::memcpy(b.data(), "ANDROID!", 8); u32(b, 8, 100); u32(b, 12, 200);
    u32(b, 20, version == 3 ? 1580 : 1584); u32(b, 40, version);
    if (version == 4) u32(b, 1580, 30);
    return b;
}
static std::vector<uint8_t> vendor(unsigned version) {
    std::vector<uint8_t> b(24576);
    std::memcpy(b.data(), "VNDRBOOT", 8); u32(b, 8, version); u32(b, 12, 4096);
    u32(b, 24, 100); u32(b, 2096, version == 3 ? 2112 : 2128); u32(b, 2100, 200);
    if (version == 4) {
        u32(b, 2112, 108); u32(b, 2116, 1); u32(b, 2120, 108); u32(b, 2124, 4);
        u32(b, 12288, 100); u32(b, 12296, 1);
    }
    return b;
}
static void be32(std::vector<uint8_t>& b, size_t p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = uint8_t(n >> (24 - i * 8));
}
static std::vector<uint8_t> dtb() {
    std::vector<uint8_t> b(100);
    be32(b, 0, 0xd00dfeed); be32(b, 4, 100); be32(b, 8, 56); be32(b, 12, 92);
    be32(b, 16, 40); be32(b, 20, 17); be32(b, 24, 16); be32(b, 32, 8); be32(b, 36, 36);
    be32(b, 56, 1); // root + empty NUL name occupies 8 bytes
    be32(b, 64, 3); be32(b, 68, 4); be32(b, 72, 0); be32(b, 76, 1234);
    be32(b, 80, 2); be32(b, 84, 4); be32(b, 88, 9);
    std::memcpy(b.data() + 92, "example", 8);
    return b;
}
int main() {
    using namespace kbl;
    Config config{}; Error error{};
    const char* valid = "# menu\r\ndefault=linux\r\ntimeout=3\r\n"
        "entry=linux|Linux|efi|/Linux.efi|console=ttyAMA0|/initrd\n"
        "entry=exit|Firmware|exit|||\n";
    check(parse_config(text(valid), config, error), "valid CRLF menu");
    check(config.count == 2 && config.timeout_seconds == 3, "configuration values");
    auto reject = [&](const char* s) {
        Config old = config;
        check(!parse_config(text(s), config, error), s);
        check(config.count == old.count && config.timeout_seconds == old.timeout_seconds, "transactional parser");
    };
    reject("entry=x|X|efi|/../bad||\n");
    reject("entry=x|X|efi|//bad||\n");
    reject("entry=x|X|efi|/dir/./bad||\n");
    reject("entry=x|X|efi|C:/bad||\n");
    reject("entry=x|X|efi|/file|a|b|extra\n");
    reject("entry=x|X|exit|||\nentry=x|Y|exit|||\n");
    reject("default=nope\nentry=x|X|exit|||\n");
    reject("timeout=4294967296\nentry=x|X|exit|||\n");
    reject("timeout=1\nentry=x|X|poweroff|||\n");
    reject("timeout=1\nentry=x|X|linux|/Image||/initrd\n");
    reject("entry=x|X|linux|/../Image||/initrd\n");
    reject("timeout=0\ntimeout=0\nentry=x|X|exit|||\n");
    reject("entry=x|X|restart|/bad||\n");
    reject("entry=x|X|unknown|||\n");
    reject("garbage=1\n");
    check(!absolute_path(text("/trailing/")) && absolute_path(text("/EFI/Linux.efi")), "path boundaries");
    Menu menu{}; menu.start(config, 1000);
    check(menu.remaining(config, 1999) == 3, "timeout rounding");
    check(!menu.update(config, Key::none, 3999), "no early auto boot");
    check(menu.update(config, Key::none, 4000), "auto boot deadline");
    check(!menu.update(config, Key::none, 9000), "one automatic attempt");
    menu.start(config, 0);
    menu.update(config, Key::up, 20);
    check(menu.selected == 1 && !menu.automatic, "up wraps and cancels timeout");
    menu.update(config, Key::down, 20);
    check(menu.selected == 0, "down wraps");
    check(!menu.update(config, Key::none, 6000), "keyboard cancellation sticks");
    menu.start(config, 0); menu.boot_failed();
    check(!menu.update(config, Key::none, 6000), "failed payload cannot create automatic loop");
    for (unsigned version = 0; version < 3; ++version) {
        auto b = image(version); AndroidImage parsed{}; const char* why = nullptr;
        check(inspect_android({b.data(), b.size()}, parsed, why), "Android boot versions 0..2");
        check(parsed.kernel.offset == 4096 && parsed.ramdisk.offset == 8192, "aligned regions");
        if (version == 2) check(parsed.dtb.offset == 16384, "DTB follows recovery DTBO");
        check(!inspect_android({b.data(), 4150}, parsed, why), "truncated payload");
        u32(b, 8, 0xffffffff);
        check(!inspect_android({b.data(), b.size()}, parsed, why), "hostile payload length");
    }
    auto b = image(2); AndroidImage parsed{}; const char* why = nullptr;
    u32(b, 1636, 4096);
    check(!inspect_android({b.data(), b.size()}, parsed, why), "overlapping DTBO rejected");
    b = image(2); u32(b, 36, 4095);
    check(!inspect_android({b.data(), b.size()}, parsed, why), "non-power-of-two pages");
    b = image(3);
    check(!inspect_android({b.data(), b.size()}, parsed, why), "invalid v3 is not silently read as v2");
    for (unsigned version = 3; version <= 4; ++version) {
        b = gki(version);
        check(inspect_android({b.data(), b.size()}, parsed, why), "valid GKI v3/v4 boot");
        check(parsed.kernel.offset == 4096 && parsed.ramdisk.offset == 8192, "GKI fixed page layout");
        if (version == 4) check(parsed.signature.offset == 12288 && parsed.signature.size == 30, "v4 signature layout");
        check(!inspect_android({b.data(), 8290}, parsed, why), "GKI truncated ramdisk");
        u32(b, 24, 1);
        check(!inspect_android({b.data(), b.size()}, parsed, why), "GKI reserved fields validated");
        b = vendor(version); VendorImage v{};
        check(inspect_vendor({b.data(), b.size()}, v, why), "vendor boot v3/v4");
        check(v.ramdisk.offset == 4096 && v.dtb.offset == 8192, "vendor aligned layout");
        check(!inspect_vendor({b.data(), 8290}, v, why), "vendor truncated DTB");
    }
    b = vendor(4); VendorImage v{}; u32(b, 2116, 0xffffffff);
    check(!inspect_vendor({b.data(), b.size()}, v, why), "vendor table count bounded");
    b = vendor(4); u32(b, 12292, 50);
    check(!inspect_vendor({b.data(), b.size()}, v, why), "vendor fragment bounds");
    b = vendor(4); u32(b, 12296, 4);
    check(!inspect_vendor({b.data(), b.size()}, v, why), "unknown vendor fragment type");
    b = vendor(4); u32(b, 2112, 216); u32(b, 2116, 2);
    u32(b, 12288+108, 50); u32(b, 12288+112, 50);
    check(!inspect_vendor({b.data(), b.size()}, v, why), "vendor fragment overlap rejected");
    b.assign(512, 0); b[0]='M'; b[1]='Z'; u32(b, 60, 64); u32(b, 64, 0x4550);
    b[68]=0x64; b[69]=0xaa; b[84]=240; b[88]=0x0b; b[89]=2; b[156]=10;
    check(inspect_efi({b.data(), b.size()}, 0xaa64, why), "ARM64 EFI header");
    check(!inspect_efi({b.data(), b.size()}, 0x8664, why), "wrong-architecture EFI rejected");
    u32(b, 60, 0xffffffff);
    check(!inspect_efi({b.data(), b.size()}, 0xaa64, why), "malformed PE offset");
    uint64_t size = 0;
    b.assign(128, 0); u32(b, 56, 0x644d5241); u32(b, 16, 128);
    check(inspect_arm64({b.data(), b.size()}, size, why) && size == 128, "ARM64 Image header");
    u32(b, 16, 4096);
    check(!inspect_arm64({b.data(), b.size()}, size, why), "truncated ARM64 Image");
    b = dtb(); Fdt tree{}; Bytes property{};
    check(Fdt::open({b.data(), b.size()}, tree, why), "valid v17 FDT");
    check(tree.property(text("/"), text("example"), property) && property.size == 4 && big32(property.data) == 1234, "FDT property lookup");
    check(!tree.property(text("/chosen"), text("example"), property), "FDT node paths respected");
    for (size_t length = 0; length < b.size(); ++length)
        check(!Fdt::open({b.data(), length}, tree, why), "every truncated FDT rejected");
    be32(b, 72, 0xffffffff);
    check(!Fdt::open({b.data(), b.size()}, tree, why), "FDT name offset bounds");
    b = dtb(); be32(b, 68, 0xffffffff);
    check(!Fdt::open({b.data(), b.size()}, tree, why), "FDT property size bounds");
    b = dtb(); be32(b, 88, 2);
    check(!Fdt::open({b.data(), b.size()}, tree, why), "FDT unmatched end node");
    b = dtb(); be32(b, 52, 1);
    check(!Fdt::open({b.data(), b.size()}, tree, why), "FDT reservation terminator required");
    b = dtb(); b.back() = 'x';
    check(!Fdt::open({b.data(), b.size()}, tree, why), "FDT property name terminator required");
    auto raw_dtb = dtb();
    b.assign(64 + raw_dtb.size(), 0);
    be32(b, 0, 0xd7b7ab1e); be32(b, 4, uint32_t(b.size())); be32(b, 8, 32);
    be32(b, 12, 32); be32(b, 16, 1); be32(b, 20, 32);
    be32(b, 32, uint32_t(raw_dtb.size())); be32(b, 36, 64);
    std::memcpy(b.data() + 64, raw_dtb.data(), raw_dtb.size());
    check(select_dtb({b.data(), b.size()}, 0, property, why) && property.size == raw_dtb.size(), "Android DT table selection");
    check(!select_dtb({b.data(), b.size()}, 1, property, why), "DT table index bounds");
    be32(b, 36, 32);
    check(!select_dtb({b.data(), b.size()}, 0, property, why), "DT table header overlap rejected");
    be32(b, 36, 64); be32(b, 32, 0xffffffff);
    check(!select_dtb({b.data(), b.size()}, 0, property, why), "DT table length bounds");
    check(!select_dtb({raw_dtb.data(), raw_dtb.size()}, 1, property, why), "raw DTB index bounds");
    std::cout << checks << " checks passed\n";
}

