#include "kbl/font.hpp"
#include "generated/jetbrains_mono_data.hpp"

namespace kbl {
uint8_t font_coverage(char glyph,unsigned x,unsigned y) {
    if(x>=font_cell_width || y>=font_cell_height) return 0;
    unsigned code=static_cast<unsigned char>(glyph);
    if(code<32 || code>126) code='?';
    const auto offset=(code-32)*font_cell_height*font_data::pitch+y*font_data::pitch+x/2;
    const auto packed=font_data::coverage[offset];
    return uint8_t(x&1?packed&15:packed>>4);
}
const char* font_license() { return font_data::license; }
} // namespace kbl
