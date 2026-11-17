#pragma once
#include "kbl/fdt.hpp"

namespace kbl {
struct LinuxImage {
    uint64_t address = 0, memory_bytes = 0;
};
// The first target uses an embedded, uncompressed, little-endian ARM64 Image.
// Its full memory span must fit below the still-executing KBL code.
bool plan_linux_image(Bytes image, uint64_t ram_base, uint64_t loader_start,
                      LinuxImage& out, const char*& error);
struct LinuxTree {
    Text arguments;
    uint64_t initrd_start = 0, initrd_bytes = 0;
    uint64_t loader_start = 0, loader_bytes = 0;
    uint64_t framebuffer = 0, framebuffer_bytes = 0;
    uint32_t width = 0, height = 0, stride = 0, stored_height = 0, planes = 0;
    bool framebuffer_no_map = true;
};
// Copy the live DT without modifying it. Replace only Linux-specific chosen
// properties, add a loader reservation, and optionally mark its framebuffer
// reservation no-map. This target requires 64-bit reserved-memory cells.
bool build_linux_tree(const Fdt& source, const LinuxTree& options,
                      uint8_t* output, size_t capacity, size_t& bytes, const char*& error);
}
