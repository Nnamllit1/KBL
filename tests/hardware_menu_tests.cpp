#include "kbl/hardware_menu.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

static void check(bool value,const char* why) {if(!value) {std::cerr<<why<<'\n';std::exit(1);}}
int main() {
    using namespace kbl;
    const std::string base="default=android\ntimeout=17\nentry=android|My Android|android|||\n";
    const std::string payload="entry=linux|My Linux|linux|/embedded/Image|rdinit=/init|/embedded/initramfs\n";
    Config config{};Error error{};
    auto parse=[&](const std::string& input,bool embedded) {
        return parse_hardware_config({input.data(),input.size()},embedded,config,error);
    };
    check(parse(base+"entry=rescue|Recovery|recovery|||\n",false),"hardware-only configuration rejected");
    check(config.timeout_seconds==17 && config.entries[0].label.view().size==10,"settings not applied");
    check(parse(base+payload,true) && config.entries[1].arguments.size==12,"embedded Linux arguments lost");
    const auto before=config;
    check(!parse(base+payload,false),"absent Linux payload accepted");
    check(config.count==before.count && config.timeout_seconds==before.timeout_seconds,"failed parse published partial state");
    check(!parse(base+"entry=efi|EFI|efi|/BOOTAA64.EFI||\n",true),"unsupported firmware action accepted");
    check(!parse("default=linux\ntimeout=0\n"+payload,true),"missing Android fallback accepted");
    check(!parse(base+payload+"entry=second|Second|linux|/embedded/Image||/embedded/initramfs\n",true),"two payload slots accepted");
    check(!parse(base+"entry=linux|Linux|linux|/disk/Image||/disk/initramfs\n",true),"unavailable filesystem path accepted");
    check(!parse(base+"unknown=1\n",false),"malformed config accepted");
    std::cout<<"Hardware configuration settings, fallback and unsupported-action rejection passed\n";
}
