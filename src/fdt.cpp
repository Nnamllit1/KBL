#include "kbl/fdt.hpp"

namespace kbl {
uint32_t big32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
uint64_t big64(const uint8_t* p) { return uint64_t(big32(p)) << 32 | big32(p + 4); }
bool select_dtb(Bytes input, size_t index, Bytes& output, const char*& error) {
    error = nullptr;
    Fdt tree{};
    if (input.size >= 4 && big32(input.data) == 0xd00dfeed) {
        if (index) { error = "raw FDT has no additional entries"; return false; }
        if (!Fdt::open(input, tree, error)) return false;
        output = tree.blob;
        return true;
    }
    if (input.size < 32 || big32(input.data) != 0xd7b7ab1e) { error = "not a raw FDT or Android DT table"; return false; }
    size_t total = big32(input.data + 4), header = big32(input.data + 8), stride = big32(input.data + 12);
    size_t count = big32(input.data + 16), offset = big32(input.data + 20);
    if (total > input.size || total < 32 || header < 32 || header > total || stride < 32 || !count ||
        offset < header || offset > total || count > (total - offset) / stride || index >= count ||
        big32(input.data + 28) != 0) { error = "invalid Android DT table header or index"; return false; }
    const uint8_t* entry = input.data + offset + stride * index;
    size_t size = big32(entry), start = big32(entry + 4);
    if (start < offset + stride * count || start > total || size > total - start) { error = "Android DT table payload exceeds bounds"; return false; }
    if (!Fdt::open({input.data + start, size}, tree, error)) return false;
    output = tree.blob;
    return true;
}
bool Fdt::open(Bytes input, Fdt& output, const char*& error) {
    error = nullptr;
    auto fail = [&](const char* why) { error = why; return false; };
    if (input.size < 40 || big32(input.data) != 0xd00dfeed) return fail("invalid FDT header");
    uint32_t total = big32(input.data + 4), version = big32(input.data + 20);
    if (total < 40 || total > input.size || version < 17 || big32(input.data + 24) > 17) return fail("unsupported or truncated FDT");
    size_t structure_offset = big32(input.data + 8), string_offset = big32(input.data + 12);
    size_t reserve_offset = big32(input.data + 16), structure_size = big32(input.data + 36);
    size_t string_size = big32(input.data + 32);
    if (structure_offset < 40 || structure_offset > total || structure_size > total - structure_offset ||
        string_offset < structure_offset + structure_size || string_offset > total || string_size > total - string_offset ||
        reserve_offset < 40 || reserve_offset > structure_offset || reserve_offset % 8 || structure_offset % 4 || structure_size % 4)
        return fail("invalid FDT section layout");
    size_t r = reserve_offset;
    for (;;) {
        if (structure_offset - r < 16) return fail("unterminated FDT reservation map");
        uint64_t address = big64(input.data + r), size = big64(input.data + r + 8);
        r += 16;
        if (!address && !size) break;
        if (size > UINT64_MAX - address) return fail("overflowing FDT reservation");
    }
    Fdt candidate{{input.data, total}, {input.data + structure_offset, structure_size},
                  {input.data + string_offset, string_size}, {input.data + reserve_offset, r - reserve_offset}};
    if (!candidate.walk(nullptr, nullptr, error)) return false;
    output = candidate;
    return true;
}
bool Fdt::walk(PropertyVisitor visit, void* context, const char*& error) const {
    error = nullptr;
    auto fail = [&](const char* why) { error = why; return false; };
    char path[1024]{};
    size_t lengths[64]{};
    bool child_seen[64]{};
    size_t depth = 0, path_length = 0, cursor = 0;
    bool root_seen = false, root_closed = false;
    while (structure.size - cursor >= 4) {
        uint32_t token = big32(structure.data + cursor);
        cursor += 4;
        if (token == 1) {
            if (depth == 64 || root_closed) return fail("invalid FDT node nesting");
            size_t start = cursor;
            while (cursor < structure.size && structure.data[cursor]) ++cursor;
            if (cursor == structure.size) return fail("unterminated FDT node name");
            Text name{reinterpret_cast<const char*>(structure.data + start), cursor - start};
            for (size_t i = 0; i < name.size; ++i) if (name.data[i] == '/') return fail("slash in FDT node name");
            lengths[depth] = path_length;
            child_seen[depth] = false;
            if (depth == 0) {
                if (root_seen || name.size) return fail("invalid FDT root node");
                root_seen = true; path[0] = '/'; path_length = 1;
            } else {
                if (!name.size || path_length + name.size + 2 >= sizeof(path)) return fail("FDT node path is too long");
                child_seen[depth - 1] = true;
                if (path_length > 1) path[path_length++] = '/';
                for (size_t i = 0; i < name.size; ++i) path[path_length++] = name.data[i];
            }
            path[path_length] = 0;
            ++depth;
            cursor = (cursor + 4) & ~size_t(3);
            if (cursor > structure.size) return fail("FDT name padding exceeds section");
        } else if (token == 2) {
            if (!depth) return fail("unmatched FDT end node");
            path_length = lengths[--depth]; path[path_length] = 0;
            if (!depth) root_closed = true;
        } else if (token == 3) {
            if (!depth || child_seen[depth - 1] || structure.size - cursor < 8) return fail("misplaced or truncated FDT property");
            size_t size = big32(structure.data + cursor), name_offset = big32(structure.data + cursor + 4);
            cursor += 8;
            if (size > structure.size - cursor || name_offset >= strings.size) return fail("FDT property exceeds section");
            size_t end = name_offset;
            while (end < strings.size && strings.data[end]) ++end;
            if (end == strings.size) return fail("unterminated FDT property name");
            if (visit && !visit({path, path_length}, {reinterpret_cast<const char*>(strings.data + name_offset), end - name_offset},
                                {structure.data + cursor, size}, context)) return true;
            cursor = (cursor + size + 3) & ~size_t(3);
            if (cursor > structure.size) return fail("FDT property padding exceeds section");
        } else if (token == 4) {
            continue;
        } else if (token == 9) {
            if (depth || !root_closed || cursor != structure.size) return fail("invalid FDT end token");
            return true;
        } else return fail("unknown FDT token");
    }
    return fail("FDT end token is missing");
}
bool Fdt::property(Text path, Text name, Bytes& value) const {
    struct Query { Text path, name; Bytes result; bool found = false; } query{path, name, {}, false};
    const char* error = nullptr;
    bool ok = walk([](Text p, Text n, Bytes bytes, void* context) {
        auto& q = *static_cast<Query*>(context);
        if (equal(p, q.path) && equal(n, q.name)) { q.result = bytes; q.found = true; return false; }
        return true;
    }, &query, error);
    if (ok && query.found) value = query.result;
    return ok && query.found;
}
} // namespace kbl
