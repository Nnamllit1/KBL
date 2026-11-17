#include "display_probe.hpp"
#include "kbl/font.hpp"

namespace kbl::gta9wifi {
namespace {
uint32_t little32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint64_t little64(const uint8_t* p) { return little32(p) | uint64_t(little32(p + 4)) << 32; }
struct Glyph { char c; uint8_t row[7]; };
// Handwritten 5x7 glyphs. No external font asset or runtime dependency.
constexpr Glyph glyphs[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{31,4,4,4,4,4,31}}, {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'+',{0,4,4,31,4,4,0}}, {'.',{0,0,0,0,0,12,12}},
    {'-',{0,0,0,31,0,0,0}}, {'|',{4,4,4,4,4,4,4}},
    {':',{0,12,12,0,12,12,0}}, {'/',{1,2,2,4,8,8,16}},
    {'(',{2,4,8,8,8,4,2}}, {')',{8,4,2,2,2,4,8}},
    {'>',{16,8,4,2,4,8,16}}, {'?',{14,17,1,2,4,0,4}}
};
uint32_t cell_pixel(TextCell cell,const MenuTheme& theme,unsigned x,unsigned y) {
    const bool selected=cell.style==CellStyle::selected;
    const auto foreground=selected?theme.selection_foreground:cell.style==CellStyle::muted?theme.muted:theme.foreground;
    const auto background=selected?theme.selection_background:theme.background;
    const unsigned alpha=font_coverage(cell.glyph,x,y);
    if(!alpha) return background;
    if(alpha==15) return foreground;
    uint32_t result=0xff000000;
    for(unsigned shift=0;shift<24;shift+=8) {
        const auto fg=(foreground>>shift)&255,bg=(background>>shift)&255;
        result|=((fg*alpha+bg*(15-alpha)+7)/15)<<shift;
    }
    return result;
}
void box(volatile uint32_t* pixels, const Display& d, unsigned plane, unsigned x, unsigned y,
         unsigned width, unsigned height, uint32_t color) {
    if (x >= d.width || y >= d.height) return;
    if (width > d.width - x) width = d.width - x;
    if (height > d.height - y) height = d.height - y;
    size_t start = size_t(plane) * d.stride * d.stored_height + size_t(y) * d.stride + x;
    for (unsigned row = 0; row < height; ++row)
        for (unsigned col = 0; col < width; ++col) pixels[start + size_t(row) * d.stride + col] = color;
}
void label(volatile uint32_t* pixels, const Display& d, unsigned plane, const char* s,
           unsigned y, unsigned scale, uint32_t color) {
    size_t length = text(s).size;
    unsigned text_width = unsigned(length) * 6 * scale;
    if (text_width >= d.width) return;
    unsigned x = (d.width - text_width) / 2;
    for (size_t i = 0; i < length; ++i) {
        for (const auto& glyph : glyphs) if (glyph.c == s[i]) {
            for (unsigned row = 0; row < 7; ++row)
                for (unsigned col = 0; col < 5; ++col)
                    if (glyph.row[row] & (1u << (4 - col))) box(pixels, d, plane, x + col * scale, y + row * scale, scale, scale, color);
            break;
        }
        x += 6 * scale;
    }
}
} // namespace

bool discover_display(const Fdt& tree, Display& display, const char*& error) {
    error = nullptr;
    auto fail = [&](const char* why) { error = why; return false; };
    Bytes model{}, legacy{}, lo{}, hi{}, size{};
    if (!tree.property(text("/"), text("model"), model) || !model.size) return fail("missing platform identity");
    Text identity{reinterpret_cast<const char*>(model.data), model.size - (model.data[model.size-1] == 0 ? 1 : 0)};
    if (!equal(identity, text("MT8781V/NA")) && !equal(identity, text("MT6789"))) return fail("unexpected platform");
    if (!tree.property(text("/chosen"), text("atag,videolfb"), legacy) || legacy.size < 20) return fail("missing inherited framebuffer");
    Display d{};
    d.address = little64(legacy.data); d.reserved_bytes = little32(legacy.data + 16);
    if (little32(legacy.data + 8) != 1) return fail("bootloader did not find a panel");
    if (tree.property(text("/chosen"), text("atag,videolfb-fb_base_l"), lo) &&
        tree.property(text("/chosen"), text("atag,videolfb-fb_base_h"), hi)) {
        if (lo.size != 4 || hi.size != 4 || (uint64_t(big32(hi.data)) << 32 | big32(lo.data)) != d.address)
            return fail("framebuffer address properties disagree");
    }
    if (tree.property(text("/chosen"), text("atag,videolfb-vramSize"), size) &&
        (size.size != 4 || big32(size.data) != d.reserved_bytes)) return fail("framebuffer size properties disagree");
    constexpr uint64_t dram_end = 0x140000000ull;
    if (d.address < 0x40100000 || d.address >= dram_end || d.address % 4 ||
        d.reserved_bytes > 64 * 1024 * 1024 || d.reserved_bytes > dram_end - d.address) return fail("framebuffer outside target RAM limits");
    uint64_t plane_bytes = uint64_t(d.stride) * d.stored_height * 4;
    if (d.reserved_bytes < plane_bytes) return fail("framebuffer reservation is too small");
    d.buffers = uint32_t(d.reserved_bytes / plane_bytes);
    if (d.buffers > 3) d.buffers = 3;
    display = d;
    return true;
}
bool draw_probe(volatile uint32_t* pixels, size_t capacity, const Display& d, unsigned exception_level, const char* footer) {
    uint64_t count = uint64_t(d.stride) * d.stored_height * d.buffers;
    if (!pixels || d.width != 800 || d.height != 1340 || d.stride != 800 || d.stored_height != 1344 ||
        !d.buffers || d.buffers > 3 || count > capacity || count > d.reserved_bytes / 4) return false;
    for (size_t i = 0; i < size_t(count); ++i) pixels[i] = 0xff101820;
    for (unsigned plane = 0; plane < d.buffers; ++plane) {
        box(pixels, d, plane, 0, 0, d.width, 24, 0xff00c7aa);
        label(pixels, d, plane, "KBL", 190, 10, 0xffffffff);
        label(pixels, d, plane, "KERNEL BOOTLOADER", 310, 5, 0xff7fffc0);
        label(pixels, d, plane, "DISPLAY PROBE", 400, 4, 0xffb0c4de);
        label(pixels, d, plane, "SM-X110", 530, 7, 0xffffffff);
        label(pixels, d, plane, exception_level == 2 ? "ARM64 EL2" : "ARM64 EL1", 620, 5, 0xffffffff);
        label(pixels, d, plane, "800 X 1340", 700, 5, 0xffb0c4de);
        for (unsigned bar = 0; bar < 3; ++bar) {
            const uint32_t colors[] = {0xffff4040, 0xff40ff40, 0xff4080ff};
            box(pixels, d, plane, 90 + 210 * bar, 850, 190, 110, colors[bar]);
        }
        label(pixels, d, plane, footer, 1080, 5, 0xffffffff);
    }
    return true;
}
bool draw_menu(volatile uint32_t* pixels, size_t capacity, const Display& d,
               const Config& config, const Menu& menu, uint64_t now_ms) {
    MenuRenderer renderer{};MenuDamage damage{};
    return render_menu(pixels,capacity,d,config,menu,now_ms,renderer,damage);
}
bool render_menu(volatile uint32_t* pixels,size_t capacity,const Display& d,const Config& config,
                 const Menu& menu,uint64_t now_ms,MenuRenderer& renderer,MenuDamage& damage,
                 const MenuTheme& theme,const char* status) {
    damage={};
    const auto count = uint64_t(d.stride) * d.stored_height * d.buffers;
    if (!pixels || d.width != 800 || d.height != 1340 || d.stride != 800 || d.stored_height != 1344 ||
        !d.buffers || d.buffers > 3 || count > capacity || count > d.reserved_bytes / 4 ||
        theme.scale<1 || theme.scale>2) return false;
    const auto cw=font_cell_width*theme.scale,ch=font_cell_height*theme.scale;
    unsigned columns=(d.width-64)/cw,rows=(d.height-64)/ch;
    if(columns>terminal_max_columns) columns=terminal_max_columns;
    if(rows>terminal_max_rows) rows=terminal_max_rows;
    MenuFrame frame{};
    if(!build_menu_frame(config,menu,now_ms,columns,rows,frame,status)) return false;
    const bool full=!renderer.initialized || !same_theme(renderer.theme,theme) ||
        renderer.previous.columns!=columns || renderer.previous.rows!=rows ||
        renderer.previous.box.x!=frame.box.x || renderer.previous.box.y!=frame.box.y ||
        renderer.previous.box.width!=frame.box.width || renderer.previous.box.height!=frame.box.height;
    if(full) {
        for(size_t i=0;i<size_t(count);++i) pixels[i]=theme.background;
        damage.pixels_written=count;
        const auto x=32+frame.box.x*cw,y=32+frame.box.y*ch;
        const auto width=frame.box.width*cw,height=frame.box.height*ch;
        // Real pixel geometry; font glyphs are used only for the text.
        for(unsigned plane=0;plane<d.buffers;++plane) {
            box(pixels,d,plane,x,y,width,2,theme.border);
            box(pixels,d,plane,x,y+height-2,width,2,theme.border);
            box(pixels,d,plane,x,y,2,height,theme.border);
            box(pixels,d,plane,x+width-2,y,2,height,theme.border);
        }
        damage.pixels_written+=uint64_t(width+height)*4*d.buffers;
    }
    for(unsigned row=0;row<rows;++row) {
        unsigned first=columns,last=0;
        for(unsigned col=0;col<columns;++col) {
            const auto cell=frame.at(col,row);
            const TextCell previous=full?TextCell{}:renderer.previous.at(col,row);
            if(cell==previous) continue;
            const auto before=damage.pixels_written;
            // Each changed pixel is written directly to its final colour.
            // No full-screen clear, temporary blank glyph, or scanout flip.
            for(unsigned y=0;y<ch;++y) for(unsigned x=0;x<cw;++x) {
                const auto next=cell_pixel(cell,theme,x/theme.scale,y/theme.scale);
                if(next==cell_pixel(previous,theme,x/theme.scale,y/theme.scale)) continue;
                const auto offset=size_t(32+row*ch+y)*d.stride+32+col*cw+x;
                for(unsigned plane=0;plane<d.buffers;++plane)
                    pixels[size_t(plane)*d.stride*d.stored_height+offset]=next;
                damage.pixels_written+=d.buffers;
            }
            if(before!=damage.pixels_written) { if(col<first)first=col;if(col>last)last=col; }
        }
        if(!full && first<columns) damage.rows[damage.count++]={32+first*cw,32+row*ch,(last-first+1)*cw,ch};
    }
    if(full) {damage.rows[0]={0,0,d.stride,d.stored_height};damage.count=1;}
    renderer.previous=frame;renderer.theme=theme;renderer.initialized=true;
    return true;
}
void heartbeat(volatile uint32_t* pixels, const Display& d, bool on) {
    for (unsigned plane = 0; plane < d.buffers; ++plane)
        box(pixels, d, plane, 740, 40, 24, 24, on ? 0xffffffff : 0xff101820);
}
} // namespace kbl::gta9wifi
