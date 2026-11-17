#include "kbl/boot_image.hpp"
#include "kbl/fdt.hpp"
#include "kbl/hardware_menu.hpp"
#include "controls.hpp"
#include "kbl/font.hpp"
#include "display_probe.hpp"
#include <fstream>
#include <cstring>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

static std::vector<uint8_t> read(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open input");
    auto size = file.tellg();
    if (size < 0 || size > 256 * 1024 * 1024) throw std::runtime_error("input exceeds 256 MiB limit (provide a raw partition image)");
    std::vector<uint8_t> result(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    if (!file) throw std::runtime_error("input read failed");
    return result;
}
static void json_string(kbl::Text text) {
    std::cout << '"';
    for (size_t i = 0; i < text.size; ++i) {
        unsigned char c = static_cast<unsigned char>(text.data[i]);
        if (c == '"' || c == '\\') std::cout << '\\' << c;
        else if (c < 32 || c >= 127) std::cout << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c) << std::dec;
        else std::cout << c;
    }
    std::cout << '"';
}
static void json_region(const char* name, kbl::Region r) {
    std::cout << ",\"" << name << "\":{\"offset\":" << r.offset << ",\"size\":" << r.size << '}';
}
int main(int argc, char** argv) try {
    if(argc==2 && std::string(argv[1])=="licenses") {std::cout<<kbl::font_license();return 0;}
    if (argc < 3) {
        std::cerr << "KBL (Kernel Bootloader) 0.3.3\nUsage: kbl inspect BOOT.img\n"
                     "       kbl inspect-json BOOT.img\n"
                     "       kbl check-efi IMAGE.efi\n"
                     "       kbl dtb TREE.dtb\n"
                     "       kbl check-controls TREE.dtb\n"
                     "       kbl extract-dtb TABLE.img OUTPUT.dtb [INDEX]\n"
                     "       kbl menu kbl.cfg\n"
                     "       kbl check-hardware-config kbl.cfg [--linux-embedded]\n"
                     "       kbl licenses\n";
        return 2;
    }
    const std::string command = argv[1];
    const auto data = read(argv[2]);
    const char* error = nullptr;
    if (command == "inspect" || command == "inspect-json") {
        if (data.size() >= 8 && std::memcmp(data.data(), "VNDRBOOT", 8) == 0) {
            kbl::VendorImage image{};
            if (!kbl::inspect_vendor({data.data(), data.size()}, image, error)) throw std::runtime_error(error);
            if (command == "inspect-json") {
                std::cout << "{\"kind\":\"vendor_boot\",\"version\":" << image.version << ",\"page_size\":" << image.page_size << ",\"cmdline\":";
                json_string(image.command_line.view());
                json_region("ramdisk", image.ramdisk); json_region("dtb", image.dtb);
                json_region("table", image.table); json_region("bootconfig", image.bootconfig);
                std::cout << ",\"fragments\":[";
                for (size_t i = 0; i < image.fragment_count; ++i) {
                    auto& f = image.fragments[i];
                    if (i) std::cout << ',';
                    std::cout << "{\"name\":"; json_string(f.name.view());
                    std::cout << ",\"type\":" << f.type;
                    json_region("data", f.data);
                    std::cout << ",\"board_id\":[";
                    for (size_t j = 0; j < 16; ++j) std::cout << (j ? "," : "") << f.board_id[j];
                    std::cout << "]}";
                }
                std::cout << "]}\n";
            } else {
                std::cout << "Vendor boot v" << image.version << " page_size=" << image.page_size
                          << " ramdisk_bytes=" << image.ramdisk.size << " dtb_bytes=" << image.dtb.size
                          << " fragments=" << image.fragment_count << " bootconfig_bytes=" << image.bootconfig.size << '\n';
                for (size_t i = 0; i < image.fragment_count; ++i) {
                    const auto& f = image.fragments[i];
                    std::cout << "fragment " << i << " type=" << f.type << " name=" << f.name.data
                              << " offset=" << f.data.offset << " size=" << f.data.size << '\n';
                }
            }
            return 0;
        }
        kbl::AndroidImage image{};
        if (!kbl::inspect_android({data.data(), data.size()}, image, error)) throw std::runtime_error(error);
        if (command == "inspect-json") {
            std::cout << "{\"kind\":\"boot\",\"version\":" << image.version << ",\"page_size\":" << image.page_size << ",\"cmdline\":";
            json_string(image.command_line.view());
            json_region("kernel", image.kernel); json_region("ramdisk", image.ramdisk); json_region("dtb", image.dtb);
            json_region("second", image.second); json_region("recovery_dtbo", image.recovery_dtbo); json_region("signature", image.signature);
            std::cout << "}\n";
            return 0;
        }
        auto region = [](const char* name, kbl::Region r) {
            std::cout << name << " offset=" << r.offset << " size=" << r.size << '\n';
        };
        std::cout << "Android boot v" << image.version << " page_size=" << image.page_size << '\n';
        region("kernel", image.kernel); region("ramdisk", image.ramdisk);
        region("second", image.second); region("recovery_dtbo", image.recovery_dtbo); region("dtb", image.dtb);
        std::cout << "cmdline=" << image.command_line.data << '\n';
    } else if (command == "dtb" || command == "extract-dtb" || command == "check-display" || command == "check-controls") {
        kbl::Bytes selected{};
        size_t index = command == "extract-dtb" && argc >= 5 ? std::stoull(argv[4]) : 0;
        if (!kbl::select_dtb({data.data(), data.size()}, index, selected, error)) throw std::runtime_error(error);
        if (command == "extract-dtb") {
            if (argc < 4) throw std::runtime_error("extract-dtb requires an output path");
            std::ifstream existing(argv[3], std::ios::binary);
            if (existing) throw std::runtime_error("output file already exists");
            std::ofstream output(argv[3], std::ios::binary);
            output.write(reinterpret_cast<const char*>(selected.data), static_cast<std::streamsize>(selected.size));
            if (!output) throw std::runtime_error("cannot write selected DTB");
            std::cout << "Extracted validated DTB " << index << ", " << selected.size << " bytes\n";
            return 0;
        }
        kbl::Fdt tree{};
        if (!kbl::Fdt::open(selected, tree, error)) throw std::runtime_error(error);
        if (command == "check-controls") {
            kbl::gta9wifi::Controls controls{};
            if (!kbl::gta9wifi::discover_controls(tree, controls, error)) throw std::runtime_error(error);
            std::cout << "SM-X110 control layout validated: keypad + MT6366 keys + watchdog modes + PSCI SMC\n";
            return 0;
        }
        if (command == "check-display") {
            kbl::gta9wifi::Display display{};
            if (!kbl::gta9wifi::discover_display(tree, display, error)) throw std::runtime_error(error);
            std::cout << "SM-X110 framebuffer discovery passed: base=0x" << std::hex << display.address << std::dec
                      << " reservation=" << display.reserved_bytes << " geometry=" << display.width << 'x' << display.height
                      << " stride_pixels=" << display.stride << " buffers=" << display.buffers << '\n';
            return 0;
        }
        std::cout << "FDT validated, bytes=" << tree.blob.size << " reservation entries=" << tree.reservations.size / 16 - 1 << '\n';
        const char* properties[] = {"atag,videolfb-fb_base_h", "atag,videolfb-fb_base_l", "atag,videolfb-vramSize", "atag,videolfb-fps"};
        for (auto name : properties) {
            kbl::Bytes value{};
            if (tree.property(kbl::text("/chosen"), kbl::text(name), value)) {
                std::cout << name << " bytes=" << value.size;
                if (value.size == 4) std::cout << " be32=0x" << std::hex << kbl::big32(value.data) << std::dec;
                std::cout << '\n';
            }
        }
        kbl::Bytes value{};
        if (tree.property(kbl::text("/"), kbl::text("model"), value))
            std::cout << "model=" << std::string(reinterpret_cast<const char*>(value.data), value.size && value.data[value.size-1] == 0 ? value.size-1 : value.size) << '\n';
    } else if (command == "check-efi") {
        if (!kbl::inspect_efi({data.data(), data.size()}, 0xaa64, error)) throw std::runtime_error(error);
        std::cout << "ARM64 EFI application verified\n";
    } else if (command == "check-hardware-config") {
        kbl::Config config{};kbl::Error parse_error{};
        const bool linux=argc==4 && std::string(argv[3])=="--linux-embedded";
        if(!kbl::parse_hardware_config({reinterpret_cast<const char*>(data.data()),data.size()},linux,config,parse_error))
            throw std::runtime_error(parse_error.message);
        std::cout<<"Hardware menu configuration valid: "<<config.count<<" entries, timeout "<<config.timeout_seconds<<" seconds\n";
    } else if (command == "menu") {
        kbl::Config config{}; kbl::Error parse_error{};
        if (!kbl::parse_config({reinterpret_cast<const char*>(data.data()), data.size()}, config, parse_error)) {
            std::cerr << "Line " << parse_error.line << ": " << parse_error.message << '\n'; return 1;
        }
        kbl::Menu menu{}; menu.start(config, 0);
        for (;;) {
            std::cout << "\nKBL (host preview)\n";
            for (size_t i = 0; i < config.count; ++i)
                std::cout << (menu.selected == i ? " > " : "   ") << config.entries[i].label.data << '\n';
            std::cout << "j=down k=up enter=inspect selection q=exit\n";
            std::string key;
            if (!std::getline(std::cin, key) || key == "q") break;
            auto input = key == "j" ? kbl::Key::down : key == "k" ? kbl::Key::up :
                         key.empty() ? kbl::Key::select : kbl::Key::cancel;
            if (menu.update(config, input, 0)) {
                const auto& e = config.entries[menu.selected];
                std::cout << "Selection: " << e.id.data << " " << e.image.data << '\n';
                if (e.action == kbl::Action::exit) break;
                std::cout << "Boot actions execute in firmware; this host preview does not execute them.\n";
            }
        }
    } else throw std::runtime_error("unknown command");
    return 0;
} catch (const std::exception& e) { std::cerr << "KBL: " << e.what() << '\n'; return 1; }

