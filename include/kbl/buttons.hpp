#pragma once
#include "kbl/core.hpp"

namespace kbl {
enum Button : uint8_t { volume_up = 1, volume_down = 2, power = 4 };

// Report one action on release. A button carried over from an earlier boot
// prompt must be fully released before it can select anything here.
struct Buttons {
    uint8_t candidate = 0, stable = 0, held = 0;
    uint64_t changed_ms = 0, released_ms = 0;
    bool initialized = false, armed = false;
    Key update(uint8_t sample, uint64_t now_ms);
};
} // namespace kbl
