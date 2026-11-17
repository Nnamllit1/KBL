#pragma once
#include "kbl/core.hpp"

namespace kbl {
enum class CellStyle : uint8_t { normal, muted, selected };
struct TextCell {
    char glyph=' ';
    CellStyle style=CellStyle::normal;
    bool operator==(const TextCell& other) const { return glyph==other.glyph && style==other.style; }
};
constexpr unsigned terminal_max_columns=48, terminal_max_rows=48;
struct MenuBox { unsigned x=0,y=0,width=0,height=0; };
struct MenuFrame {
    TextCell cells[terminal_max_columns*terminal_max_rows]{};
    unsigned columns=0, rows=0;
    MenuBox box{};
    TextCell& at(unsigned col,unsigned row) { return cells[row*terminal_max_columns+col]; }
    const TextCell& at(unsigned col,unsigned row) const { return cells[row*terminal_max_columns+col]; }
};
// Theme values are independent of menu policy and the device's input backend.
// External theme files and background images can be added without changing it.
struct MenuTheme {
    uint32_t background=0xff000000, foreground=0xffd0d0d0, muted=0xff909090;
    uint32_t selection_background=0xffd0d0d0, selection_foreground=0xff000000;
    uint32_t border=0xff707070;
    unsigned scale=1;
};
bool same_theme(const MenuTheme&,const MenuTheme&);
bool build_menu_frame(const Config&,const Menu&,uint64_t now_ms,unsigned columns,unsigned rows,
                      MenuFrame& out,const char* status=nullptr);
} // namespace kbl
