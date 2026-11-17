#pragma once
#include <stdint.h>
#include "kbl/generated/jetbrains_mono_metrics.hpp"

namespace kbl {
// Coverage is 0..15. The rasterized OFL font has no runtime font dependency.
uint8_t font_coverage(char glyph,unsigned x,unsigned y);
const char* font_license();
} // namespace kbl
