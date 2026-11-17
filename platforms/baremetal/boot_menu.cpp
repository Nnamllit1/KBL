#include "display_bridge.hpp"
#include "display_probe.hpp"
#ifdef KBL_LINUX_PAYLOAD
#include "kbl/linux_boot.hpp"
extern "C" uint8_t linux_image_begin[],linux_image_end[],linux_initrd_begin[],linux_initrd_end[];
extern "C" uint8_t linux_dtb_buffer[],__bridge_memory_end[];
#endif

namespace {
using namespace kbl;
using namespace kbl::gta9wifi;

uint64_t counter() { uint64_t value; __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(value)); return value; }
uint64_t frequency() { uint64_t value; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value)); return value; }

#ifdef KBL_MENU_VM_TEST
// VM-only register bus. The parser, PMIC protocol, button filter, menu and
// dispatch code are unchanged. Tablet packaging rejects builds with this bus.
uint32_t mock_fsm = 0, mock_mode = 0x17, mock_magic = 0xa5a50000;
uint32_t mock_upload=0x66262564, mock_power=0, mock_reason=0xcafebabe;
volatile uint32_t* const test = reinterpret_cast<volatile uint32_t*>(0x52000000);
uint32_t mock_buttons() {
    const auto count = test[0];
    if (count > 32) return 0;
    uint32_t buttons = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (bridge_context.menu_elapsed_ms >= test[4 + i * 2]) buttons = test[5 + i * 2];
    return buttons;
}
bool read_register(void*, uint64_t address, uint32_t& value) {
    const auto buttons = mock_buttons();
    if (address == 0x10010004) value = (buttons & volume_up) ? 0xfffd : 0xffff;
    else if (address == 0x100268a8) value = test[1] ? 4 : mock_fsm << 1;
    else if (address == 0x10026894) value = ((buttons & power) ? 0u : 2u) | ((buttons & volume_down) ? 0u : 8u);
    else if (address == 0x10007000) value = mock_mode;
    else if (address == 0x10007024) value = mock_magic;
    else if (address == 0x11ffe0) value = mock_upload;
    else if (address == 0x11ffe8) value = mock_power;
    else if (address == 0x11ffec) value = mock_reason;
    else return false;
    return true;
}
bool write_register(void*, uint64_t address, uint32_t value) {
    if (address == 0x10026880 && value == 0x28) mock_fsm = 6;
    else if (address == 0x100268a4 && value == 1) mock_fsm = 0;
    else if (address == 0x10007000) mock_mode = value;
    else if (address == 0x10007024) mock_magic = value;
    else if (address == 0x11ffe0) mock_upload=value;
    else if (address == 0x11ffe8) mock_power=value;
    else if (address == 0x11ffec) mock_reason=value;
    else if (address != 0x10007008 || value != 0x1971) return false;
    auto* mirror=reinterpret_cast<volatile uint32_t*>(0x52000400);
    mirror[0]=mock_mode;mirror[1]=mock_magic;
    mirror[2]=mock_upload;mirror[3]=mock_power;mirror[4]=mock_reason;
    return true;
}
int64_t system_reset(void*, uint32_t function) {
    auto* requested=reinterpret_cast<volatile uint32_t*>(0x52000420);
    requested[0]=mock_magic;requested[1]=mock_upload;requested[2]=mock_power;
    requested[3]=mock_reason;requested[4]=mock_mode;requested[5]=function;
#ifdef KBL_RESET_FAULT_TEST
    __asm__ volatile("brk #0xbe" ::: "memory");
#endif
    test[2]=mock_magic;test[3]=function;
    return -1; // PSCI NOT_SUPPORTED: exercise restoration and Android fallback.
}
#else
bool read_register(void*, uint64_t address, uint32_t& value) {
    value = *reinterpret_cast<volatile uint32_t*>(address);
    __asm__ volatile("dsb sy" ::: "memory");
    return true;
}
bool write_register(void*, uint64_t address, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(address) = value;
    __asm__ volatile("dsb sy" ::: "memory");
    return true;
}
int64_t system_reset(void*, uint32_t function) {
    return bridge_psci_system_reset(function);
}
#endif
const RegisterIO io{nullptr, read_register, write_register};
const FirmwareReset firmware{nullptr, system_reset};
#ifdef KBL_LINUX_PAYLOAD
bool start_linux(BridgeContext* context,const Fdt& tree,const Display& display,const Controls& controls) {
    if(context->sctlr&5) {context->reason="linux_requires_mmu_and_dcache_off";return false;}
    const char* error=nullptr;
#ifndef KBL_LINUX_SHARED_KERNEL
    const Bytes image{linux_image_begin,size_t(linux_image_end-linux_image_begin)};
    LinuxImage plan{};
    if(!plan_linux_image(image,0x40000000,KBL_LOADER_START,plan,error)) {context->reason=error;return false;}
#endif
    const char arguments[]=
#ifdef KBL_LINUX_SHARED_KERNEL
        "rdinit=/init ignore_loglevel loglevel=7 panic=0 selinux=0 firmware_class.path=/vendor/firmware"
#else
        "earlycon=kblfb console=tty0 ignore_loglevel loglevel=7 rdinit=/init maxcpus=1 panic=0 clk_ignore_unused pd_ignore_unused"
#endif
#ifdef KBL_MENU_VM_TEST
        " console=ttyAMA0"
#endif
        ;
    LinuxTree options{text(arguments),uint64_t(linux_initrd_begin),uint64_t(linux_initrd_end-linux_initrd_begin),
        KBL_LOADER_START,uint64_t(__bridge_memory_end)-KBL_LOADER_START,display.address,display.reserved_bytes,
        display.width,display.height,display.stride,display.stored_height,display.buffers};
#ifdef KBL_LINUX_SHARED_KERNEL
    options.framebuffer_no_map=false;
#else
    options.simple_framebuffer=true;
#endif
    size_t tree_bytes=0;
    if(!build_linux_tree(tree,options,linux_dtb_buffer,2*1024*1024,tree_bytes,error)) {context->reason=error;return false;}
    bridge_clean(uint64_t(linux_dtb_buffer),tree_bytes);
    bridge_clean(uint64_t(linux_initrd_begin),options.initrd_bytes);
    if(!stop_watchdog(controls,io,context->reboot)) {
        restore_reboot(io,context->reboot);context->reason="linux_watchdog_stop_failed";return false;
    }
    context->stage=10;
#ifdef KBL_LINUX_SHARED_KERNEL
    // The matched Samsung kernel already has the target's driver ABI. Reuse
    // its tested entry path with a different, independently prepared initramfs.
    context->args[0]=uint64_t(linux_dtb_buffer);
    context->args[1]=context->args[2]=context->args[3]=0;
    context->reason="linux_shared_kernel";
    return true;
#else
    context->reason="linux_handoff";
    bridge_linux_enter(plan.address,uint64_t(image.data),image.size,uint64_t(linux_dtb_buffer));
#endif
}
#endif
bool wait_tick(uint64_t until) {
    // A stalled/unavailable counter must not leave the device in this loop.
    uint64_t last=counter();unsigned unchanged=0;
    for (unsigned i = 0; i < 10000000; ++i) {
        const auto now=counter();
        if (now >= until) return true;
        if (now < last || (now==last && ++unchanged>=10000)) return false;
        if (now!=last) unchanged=0;
        last=now;
        __asm__ volatile("yield");
    }
    return false;
}
}

extern "C" void bridge_menu_cleanup(BridgeContext* context) {
    kbl::gta9wifi::restore_reboot(io, context->reboot);
}
extern "C" void bridge_menu(BridgeContext* context, const kbl::Fdt& tree, const kbl::gta9wifi::Display& display) {
    using namespace kbl;
    using namespace kbl::gta9wifi;
    Controls controls{}; const char* error = nullptr;
    if (!discover_controls(tree, controls, error)) { context->reason="control_layout_rejected"; return; }
    const auto hz = frequency();
    if (hz < 1000 || hz > 1000000000) { context->reason="invalid_counter_frequency"; return; }
    const auto began = counter();
    Config config{};
    config.count=3; config.timeout_seconds=8;
#ifdef KBL_LINUX_PAYLOAD
    config.count=4;
    const Action actions[] = {Action::android,Action::linux,Action::recovery,Action::download};
    const char* labels[] = {"Android",
#ifdef KBL_LINUX_SHARED_KERNEL
        "Linux desktop (experimental)",
#elif defined(KBL_LINUX_LIVE)
        "Linux live system (upstream)",
#else
        "Linux boot probe",
#endif
        "TWRP Recovery (experimental)","Download Mode (experimental)"};
#else
    const Action actions[] = {Action::android, Action::recovery, Action::download};
    const char* labels[] = {"Android", "TWRP Recovery", "Download Mode"};
#endif
    for (size_t i=0; i<config.count; ++i) {
        config.entries[i].action=actions[i]; config.entries[i].label.assign(text(labels[i]));
    }
    Menu menu{}; menu.start(config,0);
    Buttons buttons{};
    MenuRenderer renderer{};
    auto present=[&](const char* status=nullptr) {
        MenuDamage damage{};
        if(!render_menu(reinterpret_cast<volatile uint32_t*>(display.address),size_t(display.reserved_bytes/4),
                        display,config,menu,context->menu_elapsed_ms,renderer,damage,{},status)) {
            context->reason="menu_draw_rejected";return false;
        }
        if(damage.count==1 && damage.rows[0].width==display.stride && damage.rows[0].height==display.stored_height) {
            bridge_clean(display.address,uint64_t(display.stride)*display.stored_height*display.buffers*4);
        } else for(unsigned plane=0;plane<display.buffers;++plane) for(unsigned i=0;i<damage.count;++i) {
            const auto& r=damage.rows[i];
            for(unsigned y=0;y<r.height;++y) {
                const auto offset=(uint64_t(plane)*display.stride*display.stored_height+
                    uint64_t(r.y+y)*display.stride+r.x)*4;
                bridge_clean(display.address+offset,uint64_t(r.width)*4);
            }
        }
        return true;
    };
    size_t drawn_selection = max_entries;
    uint32_t drawn_seconds = 0xffffffff;
    bool drawn_automatic = false;
    context->stage=7; context->reason="menu";
    if(!present()) return;
    for (;;) {
        const auto now = counter();
        if (now < began) { context->reason="counter_went_backwards"; return; }
        const auto ms = (now-began)/(hz/1000);
        context->menu_elapsed_ms=ms;
        // Prototype recovery route: unattended or stuck keys return to Android.
        if (ms >= 90000) { context->reason="menu_safety_timeout"; context->stage=9; return; }
        if (!kick_watchdog(controls,io)) { context->reason="watchdog_unavailable"; return; }
        uint8_t sample=0;
        if (!sample_buttons(controls,io,sample)) { context->reason="input_unavailable"; return; }
        context->menu_keys |= sample;
        const auto key=buttons.update(sample,ms);
        const bool execute=menu.update(config,key,ms);
        context->menu_selected=menu.selected;
        if (execute) {
            if (config.entries[menu.selected].action==Action::android) {
                if(!present("Starting Android...")) return;
                context->stage=9;
                context->reason=key==Key::select?"android_selected":"android_timeout";
                return;
            }
#ifdef KBL_LINUX_PAYLOAD
            if(config.entries[menu.selected].action==Action::linux) {
                if(!present("Starting Linux...")) return;
                if(!start_linux(context,tree,display,controls))context->stage=9;
                return;
            }
#endif
            context->stage=8;context->reason="reset_requested";
            if(!present(config.entries[menu.selected].action==Action::recovery?
                        "Starting TWRP recovery...":"Starting Download Mode...")) return;
            context->reset_result=reset_not_prepared;
            context->reset_result=request_reboot(controls,io,firmware,config.entries[menu.selected].action,context->reboot);
            restore_reboot(io,context->reboot);
            context->reason=context->reset_result==reset_not_prepared?"reset_not_prepared":"firmware_reset_returned";
            context->stage=9;
            present("Reset failed - starting Android");
            return;
        }
        const auto seconds=menu.remaining(config,ms);
        if (drawn_selection!=menu.selected || drawn_seconds!=seconds || drawn_automatic!=menu.automatic) {
            if(!present()) return;
            drawn_selection=menu.selected;drawn_seconds=seconds;drawn_automatic=menu.automatic;
        }
        if (!wait_tick(counter()+hz/100)) { context->reason="counter_stalled"; return; }
    }
}
