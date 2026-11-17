#include "display_probe.hpp"
#include "kbl/font.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

static void check(bool result, const char* why) {
    if (!result) { std::cerr << why << '\n'; std::exit(1); }
}
int main() {
    using namespace kbl::gta9wifi;
    Display d{}; d.reserved_bytes = 0xc50000; d.buffers = 3;
    constexpr uint32_t guard = 0x12345678;
    size_t plane = size_t(d.stride) * d.stored_height;
    std::vector<uint32_t> memory(plane * 3 + 2, guard);
    auto* p = memory.data() + 1;
    check(!draw_probe(p, plane * 3 - 1, d, 2), "short buffer accepted");
    check(memory[1] == guard, "failed draw modified memory");
    check(draw_probe(p, plane * 3, d, 2), "valid draw failed");
    check(memory.front() == guard && memory.back() == guard, "framebuffer guard corrupted");
    check(p[0] == 0xff00c7aa && p[100 * d.stride] == 0xff101820, "background/border");
    check(p[860 * d.stride + 100] == 0xffff4040, "red patch");
    check(p[860 * d.stride + 320] == 0xff40ff40, "green patch");
    check(p[860 * d.stride + 530] == 0xff4080ff, "blue patch");
    bool glyph = false;
    for (unsigned x = 160; x < 640; ++x) if (p[190 * d.stride + x] == 0xffffffff) glyph = true;
    check(glyph, "title glyphs missing");
    for (size_t i = 0; i < plane; ++i)
        check(p[i] == p[plane + i] && p[i] == p[2 * plane + i], "buffer copies differ");
    heartbeat(p, d, true);
    for (unsigned i = 0; i < 3; ++i) check(p[plane * i + 40 * d.stride + 740] == 0xffffffff, "heartbeat missing");
    check(memory.front() == guard && memory.back() == guard, "heartbeat guard corrupted");
    kbl::Config config{};config.count=3;config.timeout_seconds=8;
    config.entries[0].label.assign(kbl::text("ANDROID"));
    config.entries[1].label.assign(kbl::text("TWRP RECOVERY"));
    config.entries[2].label.assign(kbl::text("DOWNLOAD MODE"));
    kbl::Menu menu{};menu.start(config,0);
    check(draw_menu(p,plane*3,d,config,menu,1000),"menu draw failed");
    check(memory.front()==guard&&memory.back()==guard,"menu framebuffer guard corrupted");
    check(p[261*d.stride+50]==0xffd0d0d0&&p[337*d.stride+50]==0xff000000,"selected terminal row not highlighted");
    menu.selected=2;
    check(draw_menu(p,plane*3,d,config,menu,2000),"last menu entry draw failed");
    check(p[261*d.stride+50]==0xff000000&&p[413*d.stride+50]==0xffd0d0d0,"selection highlight did not move");
    check(!draw_menu(p,plane*3-1,d,config,menu,2000),"menu accepted undersized framebuffer");
    menu.selected=3;check(!draw_menu(p,plane*3,d,config,menu,2000),"menu accepted out-of-range entry");
    check(memory.front()==guard&&memory.back()==guard,"invalid menu damaged framebuffer guard");
    menu.selected=0;
    MenuRenderer renderer{};MenuDamage damage{};
    check(render_menu(p,plane*3,d,config,menu,1000,renderer,damage),"incremental initial render");
    check(p[184*d.stride+100]==0xff707070&&p[300*d.stride+32]==0xff707070,
          "continuous graphical border is missing");
    check(damage.count==1&&damage.rows[0].height==d.stored_height,"initial frame did not invalidate full scanout");
    check(render_menu(p,plane*3,d,config,menu,1100,renderer,damage)&&damage.pixels_written==0&&damage.count==0,
          "unchanged frame redrew pixels");
    check(render_menu(p,plane*3,d,config,menu,2000,renderer,damage),"countdown refresh failed");
    check(damage.count==1&&damage.rows[0].y==1210&&damage.rows[0].height==38&&damage.pixels_written<5000,
          "countdown refreshed outside its text row");
    menu.selected=2;
    check(render_menu(p,plane*3,d,config,menu,2000,renderer,damage),"incremental selection failed");
    std::vector<uint32_t> reference(plane*3);
    check(draw_menu(reference.data(),reference.size(),d,config,menu,2000),"reference frame failed");
    for(size_t i=0;i<reference.size();++i) check(reference[i]==p[i],"incremental frame differs from full render");
    kbl::MenuTheme theme{};theme.background=0xff001020;theme.selection_background=0xffffffff;
    check(render_menu(p,plane*3,d,config,menu,2000,renderer,damage,theme)&&damage.rows[0].height==d.stored_height,
          "theme change did not invalidate old pixels");
    check(p[0]==theme.background,"theme background not applied");
    check(memory.front()==guard&&memory.back()==guard,"incremental or theme rendering corrupted guard");
    bool antialiased=false,lowercase=false;
    for(unsigned y=0;y<kbl::font_cell_height;++y) for(unsigned x=0;x<kbl::font_cell_width;++x) {
        auto a=kbl::font_coverage('a',x,y);
        if(a>0&&a<15) antialiased=true;
        if(a!=kbl::font_coverage('A',x,y)) lowercase=true;
    }
    check(antialiased&&lowercase,"real font lost antialiasing or lowercase glyphs");
    check(kbl::font_coverage('A',kbl::font_cell_width,0)==0&&kbl::font_coverage('A',0,kbl::font_cell_height)==0,
          "font read outside its glyph cell");
    std::cout << "Display bounds, glyphs, colours, buffer copies and heartbeat passed\n";
}
