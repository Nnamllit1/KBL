#pragma once
#include "kbl/boot_image.hpp"

namespace kbl {
using PropertyVisitor = bool (*)(Text path, Text name, Bytes value, void* context);
struct Fdt {
    Bytes blob, structure, strings, reservations;
    static bool open(Bytes input, Fdt& output, const char*& error);
    bool walk(PropertyVisitor visit, void* context, const char*& error) const;
    bool property(Text path, Text name, Bytes& value) const;
};
uint32_t big32(const uint8_t* p);
uint64_t big64(const uint8_t* p);
// Select one DTB from an Android DT table; raw DTBs accept only index zero.
bool select_dtb(Bytes input, size_t index, Bytes& output, const char*& error);
} // namespace kbl

