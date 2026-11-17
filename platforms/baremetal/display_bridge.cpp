#include "display_bridge.hpp"
#include "display_probe.hpp"

extern "C" {
void* memcpy(void* d, const void* s, size_t n) {
    auto* dst = static_cast<uint8_t*>(d); const auto* src = static_cast<const uint8_t*>(s);
    for (size_t i=0;i<n;++i) dst[i]=src[i];
    return d;
}
void* memset(void* d, int value, size_t n) {
    auto* dst = static_cast<uint8_t*>(d);
    for (size_t i=0;i<n;++i) dst[i]=uint8_t(value);
    return d;
}
}
extern "C" void bridge_clean(uint64_t start, uint64_t bytes) {
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uint64_t line = 4ull << ((ctr >> 16) & 15);
    if (line > 2048) return;
    for (uint64_t p=start & ~(line-1);p<start+bytes;p+=line)
        __asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}
namespace {
struct Writer {
    volatile char* data;
    size_t size=0, capacity;
    void put(char c) { if (size+1<capacity) data[size++]=c; }
    void text(const char* s) { if(s) while(*s) put(*s++); }
    void hex(uint64_t value) {
        text("0x"); bool started=false;
        for(int shift=60;shift>=0;shift-=4) {
            unsigned digit=unsigned((value>>shift)&15);
            if(digit || started || shift==0) { put("0123456789abcdef"[digit]); started=true; }
        }
    }
    void finish() { data[size]=0; }
};
}
extern "C" void bridge_mmu_marker(volatile char* target, unsigned el) {
    target[0]='K';target[1]='B';target[2]='L';target[3]='_';
    target[4]='E';target[5]=char('0'+el);target[6]='M';
}
extern "C" void bridge_attempt(BridgeContext* context) {
    using namespace kbl;
    context->stage=1; context->reason="entry";
    uint64_t address=context->args[0];
    if(address<0x40100000 || address>0x140000000ull-40 || address%8) {
        context->reason="bad_fdt_address";return;
    }
    context->stage=2;
    auto* raw=reinterpret_cast<const uint8_t*>(address);
    uint64_t bytes=big32(raw+4); context->fdt_size=bytes;
    if(bytes<40 || bytes>2*1024*1024 || bytes>0x140000000ull-address) {
        context->reason="bad_fdt_size";return;
    }
    Fdt tree{}; const char* error=nullptr;
    if(!Fdt::open({raw,size_t(bytes)},tree,error)) { context->reason=error;return; }
    context->stage=3;
    gta9wifi::Display display{};
    if(!gta9wifi::discover_display(tree,display,error)) {context->reason=error;return;}
    context->framebuffer=display.address;
    // The complete wrapper resides in the Samsung kernel staging reservation.
    if(display.address<0x48000000 ||
       (display.address<address+bytes && address<display.address+display.reserved_bytes)) {
        context->reason="framebuffer_overlap";return;
    }
    context->stage=4;context->reason="drawing";
#ifdef KBL_BRIDGE_FAULT_TEST
    __asm__ volatile("brk #0xbd" ::: "memory");
#endif
#ifdef KBL_BOOT_MENU
    context->stage=6;context->reason="display_ready";
    bridge_menu(context,tree,display);
#else
    auto* pixels=reinterpret_cast<volatile uint32_t*>(display.address);
    if(!gta9wifi::draw_probe(pixels,size_t(display.reserved_bytes/4),display,unsigned(context->el),"STARTING ANDROID")) {
        context->reason="draw_rejected";return;
    }
    context->stage=5;context->reason="cleaning";
    bridge_clean(display.address,uint64_t(display.stride)*display.stored_height*display.buffers*4);
    context->stage=6;context->reason="drawn";
#endif
}
extern "C" void bridge_cleanup(BridgeContext* context) {
#ifdef KBL_BOOT_MENU
    bridge_menu_cleanup(context);
#else
    (void)context;
#endif
}
extern "C" void bridge_finalize(BridgeContext* context) {
    Writer w{reinterpret_cast<volatile char*>(KBL_PROC_FORMAT_ADDRESS),0,KBL_PROC_FORMAT_CAPACITY-7};
    w.text("%s KBL_CPP %s [KBL stage=");w.hex(context->stage);
    w.text(" el=");w.hex(context->el);w.text(" sctlr=");w.hex(context->sctlr);
    w.text(" dtb=");w.hex(context->args[0]);w.text(" fb=");w.hex(context->framebuffer);
    w.text(" fault=");w.hex(context->fault);w.text(" esr=");w.hex(context->esr);
    w.text(" elr=");w.hex(context->elr);w.text(" far=");w.hex(context->far);w.text(" reason=");
    const char* reason=context->fault?"exception":context->reason;
    if(reason) for(unsigned i=0;reason[i] && i<44;++i) {
        char c=reason[i];w.put(c==' ' || c=='%'?'_':c);
    }
#ifdef KBL_BOOT_MENU
    if(context->reset_result) {w.text(" reset=");w.hex(uint64_t(context->reset_result));}
    w.text(" selected=");w.hex(context->menu_selected);
    w.text(" keys=");w.hex(context->menu_keys);
    w.text(" ms=");w.hex(context->menu_elapsed_ms);
#endif
    w.capacity=KBL_PROC_FORMAT_CAPACITY;
    w.text("] %s\n");w.finish();
}
