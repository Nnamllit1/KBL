#include "kbl/hardware_menu.hpp"

namespace kbl {
bool parse_hardware_config(Text source, bool linux_embedded, Config& out, Error& error) {
    Config parsed{};
    if(!parse_config(source,parsed,error))return false;
    bool android=false,linux=false;
    for(size_t i=0;i<parsed.count;++i) {
        const auto& entry=parsed.entries[i];
        if(entry.action==Action::android)android=true;
        else if(entry.action==Action::linux) {
            if(!linux_embedded || linux || !equal(entry.image.view(),text("/embedded/Image")) ||
               !equal(entry.initrd.view(),text("/embedded/initramfs"))) {
                error={0,"hardware Linux entry must select the single embedded payload"};return false;
            }
            linux=true;
        } else if(entry.action!=Action::recovery && entry.action!=Action::download) {
            error={0,"action requires firmware services unavailable in the embedded stage"};return false;
        }
    }
    if(!android || parsed.entries[parsed.default_entry].action!=Action::android) {
        error={0,"hardware configuration requires Android as its default and fallback"};return false;
    }
    out=parsed;return true;
}
}
