#include "kbl/menu_view.hpp"

namespace kbl {
namespace {
void put(MenuFrame& frame,unsigned col,unsigned row,const char* text,CellStyle style=CellStyle::normal) {
    if(row>=frame.rows || col>=frame.columns || !text) return;
    for(unsigned i=0;text[i] && col+i<frame.columns;++i) frame.at(col+i,row)={text[i],style};
}
}
bool same_theme(const MenuTheme& a,const MenuTheme& b) {
    return a.background==b.background && a.foreground==b.foreground && a.muted==b.muted &&
        a.selection_background==b.selection_background && a.selection_foreground==b.selection_foreground &&
        a.border==b.border && a.scale==b.scale;
}
bool build_menu_frame(const Config& config,const Menu& menu,uint64_t now_ms,unsigned columns,unsigned rows,
                      MenuFrame& out,const char* status) {
    if(columns<32 || columns>terminal_max_columns || rows<24 || rows>terminal_max_rows ||
       !config.count || config.count>max_entries || menu.selected>=config.count ||
       6+(config.count-1)*2>=rows-13) return false;
    MenuFrame frame{};frame.columns=columns;frame.rows=rows;
    put(frame,2,0,"KBL 0.3.2");
    put(frame,2,1,"Kernel Bootloader",CellStyle::muted);
    const auto bottom=rows-9;
    frame.box={0,4,columns,bottom-4+1};
    for(size_t i=0;i<config.count;++i) {
        const auto row=6+unsigned(i)*2;
        const auto style=i==menu.selected?CellStyle::selected:CellStyle::normal;
        if(i==menu.selected) for(unsigned col=1;col+1<columns;++col) frame.at(col,row)={' ',style};
        put(frame,2,row,i==menu.selected?">":" ",style);
        // Keep the border intact even when a label is wider than the terminal.
        const auto& label=config.entries[i].label;
        for(unsigned j=0;j<label.size && j+5<columns;++j) frame.at(j+4,row)={label.data[j],style};
    }
    const auto action=config.entries[menu.selected].action;
    const char* description=action==Action::android?"Boot the installed Android system":
        action==Action::linux?"Boot a separate Linux kernel":
        action==Action::recovery?"Restart into TWRP recovery":
        action==Action::download?"Restart into Samsung Download Mode":"Start the selected entry";
    put(frame,2,bottom-2,description,CellStyle::muted);
    put(frame,2,rows-6,"Volume Up/Down: select",CellStyle::muted);
    put(frame,2,rows-5,"Power (release): boot selection",CellStyle::muted);
    if(status) put(frame,2,rows-2,status);
    else if(!menu.automatic) put(frame,2,rows-2,"Automatic boot paused");
    else {
        char countdown[64]{};unsigned used=0;
        for(const char* p="Automatic boot in ";*p;++p) countdown[used++]=*p;
        auto seconds=menu.remaining(config,now_ms);
        char digits[10];unsigned n=0;
        do {digits[n++]=char('0'+seconds%10);seconds/=10;} while(seconds);
        while(n) countdown[used++]=digits[--n];
        for(const char* p=" seconds";*p;++p) countdown[used++]=*p;
        put(frame,2,rows-2,countdown);
    }
    out=frame;
    return true;
}
} // namespace kbl
