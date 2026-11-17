#pragma once
// Small UEFI 2.x ABI subset. Layouts and GUIDs follow the UEFI specification.
// No firmware implementation is linked into the portable core.
#include <stddef.h>
#include <stdint.h>

#if defined(__x86_64__)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif
namespace efi {
using Status = uintptr_t;
using Handle = void*;
using Event = void*;
using Char = char16_t;
constexpr Status success = 0;
constexpr Status error_bit = Status(1) << (sizeof(Status) * 8 - 1);
constexpr Status invalid_parameter = error_bit | 2;
constexpr Status unsupported = error_bit | 3;
constexpr Status buffer_too_small = error_bit | 5;
constexpr Status not_ready = error_bit | 6;
constexpr Status out_of_resources = error_bit | 9;
constexpr Status not_found = error_bit | 14;
inline bool failed(Status s) { return (s & error_bit) != 0; }
struct Guid { uint32_t a; uint16_t b, c; uint8_t d[8]; };
struct Header { uint64_t signature; uint32_t revision, size, crc, reserved; };
struct DevicePath { uint8_t type, subtype; uint8_t length[2]; };
struct InputKey { uint16_t scan; Char unicode; };
struct Input {
    void* reset;
    Status (EFIAPI *read)(Input*, InputKey*);
    Event wait;
};
struct Output {
    void* reset;
    Status (EFIAPI *write)(Output*, const Char*);
    void* test;
    void* query_mode;
    void* set_mode;
    Status (EFIAPI *attribute)(Output*, size_t);
    Status (EFIAPI *clear)(Output*);
    Status (EFIAPI *cursor)(Output*, size_t, size_t);
    Status (EFIAPI *enable_cursor)(Output*, uint8_t);
    void* mode;
};
struct File {
    uint64_t revision;
    Status (EFIAPI *open)(File*, File**, const Char*, uint64_t, uint64_t);
    Status (EFIAPI *close)(File*);
    void* remove;
    Status (EFIAPI *read)(File*, size_t*, void*);
    void* write;
    void* get_position;
    void* set_position;
    Status (EFIAPI *get_info)(File*, const Guid*, size_t*, void*);
};
struct FileSystem {
    uint64_t revision;
    Status (EFIAPI *open_volume)(FileSystem*, File**);
};
struct System;
struct LoadedImage {
    uint32_t revision;
    Handle parent;
    System* system;
    Handle device;
    DevicePath* path;
    void* reserved;
    uint32_t options_size;
    void* options;
    void* image_base;
    uint64_t image_size;
    uint32_t code_type, data_type;
    void* unload;
};
struct LoadFile2 {
    Status (EFIAPI *load)(LoadFile2*, DevicePath*, uint8_t, size_t*, void*);
};
struct Runtime {
    Header header;
    void *get_time, *set_time, *get_wakeup, *set_wakeup;
    void *set_virtual_map, *convert_pointer, *get_variable, *next_variable, *set_variable;
    void* next_high_count;
    void (EFIAPI *reset)(uint32_t, Status, size_t, const void*);
};
struct Boot {
    Header header;
    void *raise_tpl, *restore_tpl, *allocate_pages, *free_pages, *memory_map;
    Status (EFIAPI *allocate_pool)(uint32_t, size_t, void**);
    Status (EFIAPI *free_pool)(void*);
    Status (EFIAPI *create_event)(uint32_t, size_t, void*, void*, Event*);
    Status (EFIAPI *set_timer)(Event, uint32_t, uint64_t);
    Status (EFIAPI *wait)(size_t, Event*, size_t*);
    void* signal;
    Status (EFIAPI *close_event)(Event);
    void* check_event;
    Status (EFIAPI *install)(Handle*, const Guid*, uint32_t, void*);
    void* reinstall;
    Status (EFIAPI *uninstall)(Handle, const Guid*, void*);
    Status (EFIAPI *handle_protocol)(Handle, const Guid*, void**);
    void *reserved, *register_notify, *locate_handle, *locate_device_path, *install_table;
    Status (EFIAPI *load_image)(uint8_t, Handle, DevicePath*, void*, size_t, Handle*);
    Status (EFIAPI *start_image)(Handle, size_t*, Char**);
    void* exit;
    Status (EFIAPI *unload_image)(Handle);
    void *exit_boot_services, *monotonic;
    Status (EFIAPI *stall)(size_t);
    Status (EFIAPI *watchdog)(size_t, uint64_t, size_t, const Char*);
};
struct System {
    Header header;
    Char* vendor;
    uint32_t firmware_revision;
    Handle input_handle;
    Input* input;
    Handle output_handle;
    Output* output;
    Handle error_handle;
    Output* error;
    Runtime* runtime;
    Boot* boot;
    size_t table_count;
    void* tables;
};
static_assert(offsetof(System, boot) == 96, "UEFI system table ABI");
static_assert(offsetof(Boot, load_image) == 200, "UEFI boot services ABI");
static_assert(offsetof(Boot, watchdog) == 256, "UEFI boot services ABI");
static_assert(offsetof(Runtime, reset) == 104, "UEFI runtime services ABI");
static_assert(offsetof(LoadedImage, options) == 56, "UEFI loaded image ABI");
inline constexpr Guid loaded_image{0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0,0xa0,0xc9,0x69,0x72,0x3b}};
inline constexpr Guid simple_fs{0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
inline constexpr Guid file_info{0x09576e92,0x6d3f,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
inline constexpr Guid device_path{0x09576e91,0x6d3f,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
inline constexpr Guid load_file2{0x4006c0c1,0xfcb3,0x403e,{0x99,0x6d,0x4a,0x6c,0x87,0x24,0xe0,0x6d}};
inline constexpr Guid linux_initrd{0x5568e427,0x68fc,0x4f3d,{0xac,0x74,0xca,0x55,0x52,0x31,0xcc,0x68}};
} // namespace efi

