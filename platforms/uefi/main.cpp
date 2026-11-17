#include "efi.hpp"
#include "kbl/core.hpp"
#include "kbl/boot_image.hpp"

// The compiler may emit these for aggregate operations; no hosted CRT is needed.
extern "C" void* memcpy(void* d, const void* s, size_t n) {
    auto* dst = static_cast<uint8_t*>(d); const auto* src = static_cast<const uint8_t*>(s);
    for (size_t i = 0; i < n; ++i) dst[i] = src[i];
    return d;
}
extern "C" void* memset(void* d, int c, size_t n) {
    auto* dst = static_cast<uint8_t*>(d);
    for (size_t i = 0; i < n; ++i) dst[i] = static_cast<uint8_t>(c);
    return d;
}
using namespace kbl;
namespace {
efi::System* system_table;
efi::Handle self;
efi::Handle volume_device;
efi::File* root;
struct Buffer { void* data = nullptr; size_t size = 0; };
Buffer initrd;

void print(const char* s) {
    efi::Char buffer[160];
    size_t n = 0;
    while (*s) {
        if (n >= 157) { buffer[n] = 0; system_table->output->write(system_table->output, buffer); n = 0; }
        if (*s == '\n') buffer[n++] = '\r';
        buffer[n++] = static_cast<efi::Char>(*s++);
    }
    buffer[n] = 0;
    system_table->output->write(system_table->output, buffer);
}
void trace(const char* s) {
#ifdef KBL_TRACE
    print(s);
#else
    (void)s;
#endif
}
void number(uint64_t value) {
    char b[24]; size_t i = sizeof(b) - 1; b[i] = 0;
    do { b[--i] = '0' + char(value % 10); value /= 10; } while (value);
    print(b + i);
}
void hex(efi::Status value) {
    char s[19] = "0x0000000000000000";
    for (size_t i = 0; i < 16; ++i) { s[17 - i] = "0123456789abcdef"[value & 15]; value >>= 4; }
    print(s);
}
void release(Buffer& b) {
    if (b.data) system_table->boot->free_pool(b.data);
    b = {};
}
bool wide(Text s, efi::Char* out, size_t capacity, bool path = false) {
    if (s.size >= capacity) return false;
    for (size_t i = 0; i < s.size; ++i) out[i] = path && s.data[i] == '/' ? u'\\' : efi::Char(s.data[i]);
    out[s.size] = 0;
    return true;
}
efi::Status read_file(Text path, Buffer& result, size_t limit) {
    efi::Char name[256];
    if (!root || !absolute_path(path) || !wide(path, name, 256, true)) return efi::invalid_parameter;
    efi::File* file = nullptr;
    efi::Status status = root->open(root, &file, name, 1, 0);
    if (efi::failed(status)) return status;
    alignas(8) uint8_t info[1024]; size_t info_size = sizeof(info);
    status = file->get_info(file, &efi::file_info, &info_size, info);
    uint64_t file_size = 0;
    if (!efi::failed(status) && info_size >= 80) memcpy(&file_size, info + 8, 8);
    if (efi::failed(status) || !file_size || file_size > limit) {
        file->close(file);
        return efi::failed(status) ? status : efi::out_of_resources;
    }
    void* data = nullptr;
    status = system_table->boot->allocate_pool(2, size_t(file_size) + 1, &data);
    if (!efi::failed(status)) {
        size_t size = size_t(file_size);
        status = file->read(file, &size, data);
        if (!efi::failed(status) && size == file_size) {
            static_cast<uint8_t*>(data)[size] = 0;
            result = {data, size};
        } else {
            system_table->boot->free_pool(data);
            if (!efi::failed(status)) status = efi::invalid_parameter;
        }
    }
    file->close(file);
    return status;
}
efi::Status file_path(Text path, Buffer& result) {
    efi::DevicePath* base = nullptr;
    efi::Status status = system_table->boot->handle_protocol(volume_device, &efi::device_path, reinterpret_cast<void**>(&base));
    if (efi::failed(status)) return status;
    size_t prefix = 0;
    while (prefix < 8192) {
        const auto* p = reinterpret_cast<const efi::DevicePath*>(reinterpret_cast<uint8_t*>(base) + prefix);
        size_t length = p->length[0] | size_t(p->length[1]) << 8;
        if (length < 4 || length > 8192 - prefix) return efi::invalid_parameter;
        if (p->type == 0x7f && p->subtype == 0xff) break;
        prefix += length;
    }
    if (prefix >= 8192) return efi::invalid_parameter;
    const size_t path_length = 4 + (path.size + 1) * 2;
    status = system_table->boot->allocate_pool(2, prefix + path_length + 4, &result.data);
    if (efi::failed(status)) return status;
    result.size = prefix + path_length + 4;
    memcpy(result.data, base, prefix);
    auto* node = reinterpret_cast<efi::DevicePath*>(static_cast<uint8_t*>(result.data) + prefix);
    *node = {4, 4, {uint8_t(path_length), uint8_t(path_length >> 8)}};
    wide(path, reinterpret_cast<efi::Char*>(node + 1), path.size + 1, true);
    auto* end = reinterpret_cast<efi::DevicePath*>(static_cast<uint8_t*>(result.data) + prefix + path_length);
    *end = {0x7f, 0xff, {4, 0}};
    return efi::success;
}
efi::Status EFIAPI load_initrd(efi::LoadFile2*, efi::DevicePath* path, uint8_t boot_policy, size_t* size, void* data) {
    if (boot_policy) return efi::unsupported;
    if (!size || !path || path->type != 0x7f || path->subtype != 0xff) return efi::invalid_parameter;
    if (!data || *size < initrd.size) { *size = initrd.size; return efi::buffer_too_small; }
    memcpy(data, initrd.data, initrd.size);
    *size = initrd.size;
    trace("KBL_INITRD_DELIVERED\n");
    return efi::success;
}
efi::LoadFile2 initrd_protocol{load_initrd};
#pragma pack(push, 1)
struct InitrdPath { efi::DevicePath vendor; efi::Guid guid; efi::DevicePath end; };
#pragma pack(pop)
InitrdPath initrd_path{{4, 3, {20, 0}}, efi::linux_initrd, {0x7f, 0xff, {4, 0}}};

efi::Status launch(const Entry& entry) {
    Buffer image{}, path{};
    efi::Handle child = nullptr, initrd_handle = nullptr;
    bool installed_path = false, installed_initrd = false;
    auto* bs = system_table->boot;
    efi::Status status = read_file(entry.image.view(), image, 512 * 1024 * 1024);
    if (efi::failed(status)) return status;
    const char* error = nullptr;
#if defined(__aarch64__)
    constexpr uint16_t machine = 0xaa64;
#else
    constexpr uint16_t machine = 0x8664;
#endif
    if (!inspect_efi({static_cast<const uint8_t*>(image.data), image.size}, machine, error)) {
        print(error); print("\n"); release(image); return efi::unsupported;
    }
    status = file_path(entry.image.view(), path);
    if (!efi::failed(status)) status = bs->load_image(0, self, static_cast<efi::DevicePath*>(path.data), image.data, image.size, &child);
    release(image); release(path);
    if (efi::failed(status)) return status;
    efi::LoadedImage* loaded = nullptr;
    status = bs->handle_protocol(child, &efi::loaded_image, reinterpret_cast<void**>(&loaded));
    efi::Char options[768];
    if (!efi::failed(status)) {
        wide(entry.arguments.view(), options, 768);
        loaded->options = options;
        loaded->options_size = uint32_t((entry.arguments.size + 1) * 2);
    }
    if (!efi::failed(status) && entry.initrd.size) {
        status = read_file(entry.initrd.view(), initrd, 512 * 1024 * 1024);
        if (!efi::failed(status)) {
            status = bs->install(&initrd_handle, &efi::device_path, 0, &initrd_path);
            installed_path = !efi::failed(status);
        }
        if (!efi::failed(status)) {
            status = bs->install(&initrd_handle, &efi::load_file2, 0, &initrd_protocol);
            installed_initrd = !efi::failed(status);
        }
    }
    if (!efi::failed(status)) {
        trace("KBL_START_IMAGE\n");
        status = bs->start_image(child, nullptr, nullptr);
        trace("KBL_IMAGE_RETURNED\n");
    }
    if (installed_initrd) bs->uninstall(initrd_handle, &efi::load_file2, &initrd_protocol);
    if (installed_path) bs->uninstall(initrd_handle, &efi::device_path, &initrd_path);
    release(initrd);
    bs->unload_image(child);
    return status;
}
void draw(const Config& config, const Menu& menu, uint64_t now, const char* message) {
    auto* out = system_table->output;
    out->attribute(out, 0x1f);
    out->clear(out);
    print("\n  KBL - Kernel Bootloader\n  Choose an operating system\n\n");
    for (size_t i = 0; i < config.count; ++i) {
        out->attribute(out, menu.selected == i ? 0x70 : 0x1f);
        print(menu.selected == i ? "  > " : "    ");
        print(config.entries[i].label.data); print("\n");
    }
    out->attribute(out, 0x1f);
    print("\n  Up/Down: choose    Enter: start    Esc: cancel timer\n");
    if (menu.automatic) { print("  Automatic boot in "); number(menu.remaining(config, now)); print(" seconds\n"); }
    if (message) { print("\n  "); print(message); print("\n"); }
    trace("KBL_MENU_READY\n");
}
} // namespace

extern "C" efi::Status EFIAPI efi_main(efi::Handle handle, efi::System* system) {
    self = handle; system_table = system;
    auto* bs = system->boot;
    bs->watchdog(0, 0, 0, nullptr);
    system->output->enable_cursor(system->output, 0);
    efi::LoadedImage* loaded = nullptr;
    efi::Status status = bs->handle_protocol(handle, &efi::loaded_image, reinterpret_cast<void**>(&loaded));
    if (efi::failed(status)) return status;
    volume_device = loaded->device;
    efi::FileSystem* fs = nullptr;
    status = bs->handle_protocol(volume_device, &efi::simple_fs, reinterpret_cast<void**>(&fs));
    if (efi::failed(status)) return status;
    status = fs->open_volume(fs, &root);
    if (efi::failed(status)) return status;
    Buffer file{};
    Config config{}; Error error{};
    status = read_file(text("/kbl.cfg"), file, 32768);
    const char* message = nullptr;
    if (efi::failed(status) || !parse_config({static_cast<const char*>(file.data), file.size}, config, error)) {
        message = error.message ? error.message : "Cannot read /kbl.cfg";
        const char fallback[] = "entry=off|Power off|poweroff|||\nentry=exit|Return to firmware|exit|||\n";
        Error unused{};
        parse_config(text(fallback), config, unused);
    }
    release(file);
    efi::Event timer = nullptr;
    status = bs->create_event(0x80000000, 4, nullptr, nullptr, &timer);
    if (efi::failed(status)) { root->close(root); return status; }
    status = bs->set_timer(timer, 1, 1000000); // 100 ms
    if (efi::failed(status)) { bs->close_event(timer); root->close(root); return status; }
    Menu menu{}; uint64_t now = 0; menu.start(config, now);
    draw(config, menu, now, message);
    for (;;) {
        efi::Event events[2]{system->input->wait, timer};
        size_t ready = 0;
        status = bs->wait(2, events, &ready);
        if (efi::failed(status)) break;
        Key key = Key::none;
        if (ready == 0) {
            efi::InputKey input{};
            if (!efi::failed(system->input->read(system->input, &input))) {
                if (input.scan == 1) key = Key::up;
                else if (input.scan == 2) key = Key::down;
                else if (input.unicode == u'\r' || input.unicode == u'\n') key = Key::select;
                else key = Key::cancel; // Every key stops the countdown.
            }
        } else now += 100;
        if (menu.update(config, key, now)) {
            const auto& entry = config.entries[menu.selected];
            if (entry.action == Action::exit) { status = efi::success; break; }
            if (entry.action == Action::poweroff || entry.action == Action::restart) {
                system->runtime->reset(entry.action == Action::poweroff ? 2 : 0, efi::success, 0, nullptr);
                message = "Firmware returned from ResetSystem";
            } else if (entry.action != Action::efi) {
                message = "This action requires a device adapter.";
            } else {
                status = launch(entry);
                if (efi::failed(status)) { print("Boot status: "); hex(status); print("\n"); }
                message = efi::failed(status) ? "Boot failed. Choose another entry." : "Payload returned. Choose another entry.";
            }
            menu.boot_failed();
            draw(config, menu, now, message);
        } else if (key != Key::none || (menu.automatic && now % 1000 == 0)) draw(config, menu, now, message);
    }
    bs->close_event(timer);
    root->close(root);
    return status;
}

