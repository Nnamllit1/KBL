#pragma once
#include <stddef.h>
#include <stdint.h>

namespace kbl {
struct Text {
    const char* data = nullptr;
    size_t size = 0;
};
Text text(const char* s);
bool equal(Text a, Text b);
bool absolute_path(Text path);

template<size_t N> struct String {
    char data[N]{};
    size_t size = 0;
    bool assign(Text s) {
        if (s.size >= N) return false;
        for (size_t i = 0; i < s.size; ++i) data[i] = s.data[i];
        size = s.size;
        data[size] = 0;
        return true;
    }
    Text view() const { return {data, size}; }
};

enum class Action { efi, restart, poweroff, exit, android, recovery, download, linux };
struct Entry {
    String<32> id;
    String<80> label;
    Action action = Action::exit;
    String<192> image;
    String<768> arguments;
    String<192> initrd;
};
constexpr size_t max_entries = 12;
struct Config {
    Entry entries[max_entries]{};
    size_t count = 0;
    size_t default_entry = 0;
    uint32_t timeout_seconds = 0;
};
struct Error {
    size_t line = 0;
    const char* message = nullptr;
};
// Parses transactionally: an error never exposes a partially parsed boot menu.
bool parse_config(Text source, Config& out, Error& error);

enum class Key { none, up, down, select, cancel };
struct Menu {
    size_t selected = 0;
    uint64_t started_ms = 0;
    bool automatic = false;
    void start(const Config& config, uint64_t now_ms);
    // Returns true when the selected entry should be executed.
    bool update(const Config& config, Key key, uint64_t now_ms);
    uint32_t remaining(const Config& config, uint64_t now_ms) const;
    void boot_failed() { automatic = false; }
};
} // namespace kbl

