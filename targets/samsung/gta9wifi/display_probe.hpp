#pragma once
#include "kbl/fdt.hpp"
#include "kbl/menu_view.hpp"

namespace kbl::gta9wifi {
struct Display {
    uint64_t address = 0;
    uint64_t reserved_bytes = 0;
    uint32_t width = 800, height = 1340, stride = 800, stored_height = 1344;
    uint32_t buffers = 0;
};
bool discover_display(const Fdt& tree, Display& display, const char*& error);
// Draw only inside the supplied framebuffer span. Used by the bare-metal
// image and by the host renderer tests.
bool draw_probe(volatile uint32_t* pixels, size_t capacity, const Display& display, unsigned exception_level,
                const char* footer = "NO OS LOADED");
bool draw_menu(volatile uint32_t* pixels, size_t capacity, const Display& display,
               const Config& config, const Menu& menu, uint64_t now_ms);
struct DamageRect { unsigned x=0,y=0,width=0,height=0; };
struct MenuDamage {
    DamageRect rows[terminal_max_rows]{};
    unsigned count=0;
    uint64_t pixels_written=0;
};
struct MenuRenderer {
    MenuFrame previous{};
    MenuTheme theme{};
    bool initialized=false;
};
bool render_menu(volatile uint32_t* pixels,size_t capacity,const Display&,const Config&,const Menu&,
                 uint64_t now_ms,MenuRenderer&,MenuDamage&,const MenuTheme& theme={},const char* status=nullptr);
void heartbeat(volatile uint32_t* pixels, const Display& display, bool on);
} // namespace kbl::gta9wifi

