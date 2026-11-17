#include "kbl/core.hpp"

namespace kbl {
Text text(const char* s) {
    size_t n = 0;
    if (s) while (s[n]) ++n;
    return {s, n};
}
bool equal(Text a, Text b) {
    if (a.size != b.size) return false;
    for (size_t i = 0; i < a.size; ++i) if (a.data[i] != b.data[i]) return false;
    return true;
}
static Text trim(Text s) {
    while (s.size && (s.data[0] == ' ' || s.data[0] == '\t')) { ++s.data; --s.size; }
    while (s.size && (s.data[s.size - 1] == ' ' || s.data[s.size - 1] == '\t' || s.data[s.size - 1] == '\r')) --s.size;
    return s;
}
static bool printable(Text s) {
    for (size_t i = 0; i < s.size; ++i) if (s.data[i] < 32 || s.data[i] > 126) return false;
    return true;
}
static bool identifier(Text s) {
    if (!s.size) return false;
    for (size_t i = 0; i < s.size; ++i) {
        char c = s.data[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}
bool absolute_path(Text p) {
    if (p.size < 2 || p.data[0] != '/' || p.data[p.size - 1] == '/') return false;
    size_t begin = 1;
    for (size_t i = 1; i <= p.size; ++i) {
        if (i < p.size && (p.data[i] < 33 || p.data[i] > 126 || p.data[i] == '\\' ||
                          p.data[i] == ':' || p.data[i] == '*' || p.data[i] == '?' || p.data[i] == '|')) return false;
        if (i == p.size || p.data[i] == '/') {
            Text part{p.data + begin, i - begin};
            if (!part.size || equal(part, text(".")) || equal(part, text(".."))) return false;
            begin = i + 1;
        }
    }
    return true;
}
bool parse_config(Text source, Config& out, Error& error) {
    Config parsed{};
    String<32> default_id{};
    bool have_timeout = false, have_default = false;
    error = {};
    if (source.size > 32768) { error = {0, "configuration exceeds 32 KiB"}; return false; }
    size_t offset = 0, line_no = 0;
    auto fail = [&](const char* why) { error = {line_no, why}; return false; };
    while (offset < source.size) {
        size_t end = offset;
        while (end < source.size && source.data[end] != '\n') ++end;
        Text line = trim({source.data + offset, end - offset});
        offset = end + 1;
        ++line_no;
        if (!line.size || line.data[0] == '#') continue;
        if (!printable(line)) return fail("configuration must contain printable ASCII");
        size_t sep = 0;
        while (sep < line.size && line.data[sep] != '=') ++sep;
        if (sep == line.size) return fail("expected key=value");
        Text key = trim({line.data, sep});
        Text value = trim({line.data + sep + 1, line.size - sep - 1});
        if (equal(key, text("timeout"))) {
            if (have_timeout || !value.size) return fail("invalid or duplicate timeout");
            have_timeout = true;
            uint32_t seconds = 0;
            for (size_t i = 0; i < value.size; ++i) {
                char c = value.data[i];
                if (c < '0' || c > '9' || seconds > 360) return fail("timeout must be 0..3600 seconds");
                seconds = seconds * 10 + static_cast<uint32_t>(c - '0');
            }
            if (seconds > 3600) return fail("timeout must be 0..3600 seconds");
            parsed.timeout_seconds = seconds;
        } else if (equal(key, text("default"))) {
            if (have_default || !identifier(value) || !default_id.assign(value)) return fail("invalid or duplicate default");
            have_default = true;
        } else if (equal(key, text("entry"))) {
            if (parsed.count == max_entries) return fail("too many boot entries");
            Text fields[6]{};
            size_t count = 0, begin = 0;
            for (size_t i = 0; i <= value.size; ++i) {
                if (i == value.size || value.data[i] == '|') {
                    if (count == 6) return fail("entry requires exactly six fields");
                    fields[count++] = trim({value.data + begin, i - begin});
                    begin = i + 1;
                }
            }
            if (count != 6) return fail("entry requires id|label|action|image|arguments|initrd");
            Entry& e = parsed.entries[parsed.count];
            if (!identifier(fields[0]) || !e.id.assign(fields[0]) || !fields[1].size ||
                !e.label.assign(fields[1]) || !e.image.assign(fields[3]) ||
                !e.arguments.assign(fields[4]) || !e.initrd.assign(fields[5])) return fail("invalid or oversized entry");
            for (size_t i = 0; i < parsed.count; ++i)
                if (equal(parsed.entries[i].id.view(), e.id.view())) return fail("duplicate entry id");
            if (equal(fields[2], text("efi"))) e.action = Action::efi;
            else if (equal(fields[2], text("restart"))) e.action = Action::restart;
            else if (equal(fields[2], text("poweroff"))) e.action = Action::poweroff;
            else if (equal(fields[2], text("exit"))) e.action = Action::exit;
            else if (equal(fields[2], text("android"))) e.action = Action::android;
            else if (equal(fields[2], text("recovery"))) e.action = Action::recovery;
            else if (equal(fields[2], text("download"))) e.action = Action::download;
            else if (equal(fields[2], text("linux"))) e.action = Action::linux;
            else return fail("unknown boot action");
            if (e.action == Action::efi || e.action == Action::linux) {
                if (!absolute_path(e.image.view()) || (e.initrd.size && !absolute_path(e.initrd.view()))) return fail("image and initrd require absolute paths without traversal");
            } else if (e.image.size || e.arguments.size || e.initrd.size) return fail("non-image action has image arguments");
            ++parsed.count;
        } else return fail("unknown configuration key");
    }
    if (!parsed.count) return fail("no boot entries");
    if (have_default) {
        bool found = false;
        for (size_t i = 0; i < parsed.count; ++i) {
            if (equal(default_id.view(), parsed.entries[i].id.view())) { parsed.default_entry = i; found = true; }
        }
        if (!found) return fail("default entry does not exist");
    }
    // Recovery, reset and power actions always require explicit selection.
    const auto automatic = parsed.entries[parsed.default_entry].action;
    if (parsed.timeout_seconds && automatic != Action::efi && automatic != Action::android)
        return fail("automatic boot requires an EFI payload or Android");
    out = parsed;
    return true;
}
void Menu::start(const Config& config, uint64_t now_ms) {
    selected = config.default_entry;
    started_ms = now_ms;
    automatic = config.count && config.timeout_seconds;
}
uint32_t Menu::remaining(const Config& config, uint64_t now_ms) const {
    if (!automatic || now_ms < started_ms) return 0;
    uint64_t elapsed = (now_ms - started_ms) / 1000;
    return elapsed >= config.timeout_seconds ? 0 : config.timeout_seconds - static_cast<uint32_t>(elapsed);
}
bool Menu::update(const Config& config, Key key, uint64_t now_ms) {
    if (!config.count) return false;
    if (key != Key::none) automatic = false;
    switch (key) {
    case Key::up: selected = selected ? selected - 1 : config.count - 1; break;
    case Key::down: selected = (selected + 1) % config.count; break;
    case Key::select: return true;
    default: break;
    }
    if (automatic && now_ms >= started_ms && now_ms - started_ms >= uint64_t(config.timeout_seconds) * 1000) {
        automatic = false;
        return true;
    }
    return false;
}
} // namespace kbl

