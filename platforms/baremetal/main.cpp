#include "kbl/fdt.hpp"
#include "display_probe.hpp"

extern "C" void* memcpy(void* d, const void* s, size_t size) {
    auto* dst = static_cast<uint8_t*>(d); const auto* src = static_cast<const uint8_t*>(s);
    for (size_t i = 0; i < size; ++i) dst[i] = src[i];
    return d;
}
extern "C" void* memset(void* d, int value, size_t size) {
    auto* dst = static_cast<uint8_t*>(d);
    for (size_t i = 0; i < size; ++i) dst[i] = uint8_t(value);
    return d;
}
extern "C" char _image_start[], __stack_top[];
namespace {
void serial(const char* s) {
#ifdef KBL_PROBE_QEMU
    auto* uart = reinterpret_cast<volatile uint32_t*>(0x09000000);
    uart[12] = 0x301; // QEMU PL011: UART, transmitter and receiver enabled.
    while (*s) {
        while (uart[6] & 0x20) __asm__ volatile("yield");
        uart[0] = uint8_t(*s++);
    }
#else
    (void)s;
#endif
}
void clean(uint64_t start, uint64_t bytes) {
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uint64_t line = 4ull << ((ctr >> 16) & 15);
    if (line > 2048) return;
    for (uint64_t p = start & ~(line - 1); p < start + bytes; p += line)
        __asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}
[[noreturn]] void halt(const char* reason) {
    serial(reason); serial("\n");
    for (;;) __asm__ volatile("wfe");
}
bool overlaps(uint64_t a, uint64_t as, uint64_t b, uint64_t bs) { return a < b + bs && b < a + as; }
volatile uint32_t* watchdog(const kbl::Fdt& tree) {
    using namespace kbl;
    Bytes compatible{}, reg{};
    const auto path = text("/soc/watchdog@10007000");
    if (!tree.property(path, text("compatible"), compatible) || compatible.size < 20 ||
        !equal({reinterpret_cast<const char*>(compatible.data), 19}, text("mediatek,mt6789-wdt")) ||
        !tree.property(path, text("reg"), reg) || reg.size != 16 || big64(reg.data) != 0x10007000 ||
        big64(reg.data + 8) < 12) return nullptr;
    return reinterpret_cast<volatile uint32_t*>(0x10007008);
}
}
extern "C" void probe_main(uint64_t fdt_address, unsigned exception_level) {
    using namespace kbl;
    serial("KBL_BARE_METAL_ENTRY\n");
    if ((exception_level != 1 && exception_level != 2) || fdt_address < 0x40100000 ||
        fdt_address > 0x140000000ull - 40 || fdt_address % 8) halt("BAD_ENTRY_CONTEXT");
    auto* header = reinterpret_cast<const uint8_t*>(fdt_address);
    uint64_t dtb_size = big32(header + 4);
    if (dtb_size < 40 || dtb_size > 2 * 1024 * 1024 || dtb_size > 0x140000000ull - fdt_address)
        halt("BAD_FDT_SIZE");
    Fdt tree{}; const char* error = nullptr;
    if (!Fdt::open({header, size_t(dtb_size)}, tree, error)) halt("BAD_FDT");
    gta9wifi::Display display{};
    if (!gta9wifi::discover_display(tree, display, error)) halt(error);
    uint64_t image_address = reinterpret_cast<uint64_t>(_image_start);
    uint64_t image_bytes = reinterpret_cast<uint64_t>(__stack_top) - image_address;
    if (overlaps(display.address, display.reserved_bytes, fdt_address, dtb_size) ||
        overlaps(display.address, display.reserved_bytes, image_address, image_bytes)) halt("FRAMEBUFFER_OVERLAPS_BOOT_CONTEXT");
    auto* pixels = reinterpret_cast<volatile uint32_t*>(display.address);
    if (!gta9wifi::draw_probe(pixels, size_t(display.reserved_bytes / 4), display, exception_level)) halt("BAD_DISPLAY_GEOMETRY");
    clean(display.address, uint64_t(display.stride) * display.stored_height * display.buffers * 4);
    serial(exception_level == 2 ? "KBL_BARE_METAL_EL2\n" : "KBL_BARE_METAL_EL1\n");
    serial("KBL_BARE_METAL_DRAWN\n");
    // Reload an already configured MT6789 watchdog only; its configuration,
    // reset-reason register and all storage are left alone.
    volatile uint32_t* reload = watchdog(tree);
    uint64_t frequency, last;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(last));
    if (!frequency || frequency > 1000000000) halt("BAD_TIMER_FREQUENCY");
    bool on = false;
    uint64_t ticks = 0;
    for (;;) {
        uint64_t now;
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
        if (now - last >= frequency / 4) {
            last = now;
            if (reload) { *reload = 0x1971; __asm__ volatile("dsb sy" ::: "memory"); }
            if (++ticks % 2 == 0) {
                on = !on;
                gta9wifi::heartbeat(pixels, display, on);
                for (unsigned plane = 0; plane < display.buffers; ++plane)
                    for (unsigned row = 40; row < 64; ++row)
                        clean(display.address + (uint64_t(plane) * display.stored_height * display.stride + row * display.stride + 736) * 4, 128);
                if (ticks == 2) serial("KBL_BARE_METAL_HEARTBEAT\n");
            }
        }
        __asm__ volatile("yield");
    }
}
