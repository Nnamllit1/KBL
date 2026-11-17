#include "kbl/buttons.hpp"

namespace kbl {
Key Buttons::update(uint8_t sample, uint64_t now_ms) {
    if (!initialized) {
        candidate = stable = sample;
        changed_ms = released_ms = now_ms;
        initialized = true;
        return Key::none;
    }
    if (sample != candidate) { candidate = sample; changed_ms = now_ms; }
    if (now_ms < changed_ms || now_ms - changed_ms < 30) return Key::none;
    if (!armed) {
        if (candidate) { released_ms = now_ms; return Key::none; }
        if (now_ms >= released_ms && now_ms - released_ms >= 80) {
            armed = true; stable = 0; held = 0;
        }
        return Key::none;
    }
    if (candidate == stable) return Key::none;
    stable = candidate;
    if (stable) {
        // A chord invalidates this press; releasing one key cannot select.
        if (held || (stable & (stable - 1))) held = 0xff;
        else held = stable;
        return Key::cancel; // Cancel autoboot as soon as a press is debounced.
    }
    const auto released = held;
    held = 0;
    if (released == volume_up) return Key::up;
    if (released == volume_down) return Key::down;
    if (released == power) return Key::select;
    return Key::none;
}
} // namespace kbl
