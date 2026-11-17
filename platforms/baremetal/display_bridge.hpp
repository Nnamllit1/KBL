#pragma once
#include <stddef.h>
#include <stdint.h>
#include "controls.hpp"
namespace kbl::gta9wifi { struct Display; }
struct BridgeContext {
    volatile uint64_t args[4], lr, sp, bank_sp, spsel, daif, vbar, el, sctlr, pc;
    volatile uint64_t stage, framebuffer, esr, elr, far, fault;
    const char* volatile reason;
    volatile uint64_t fdt_size, active, fault_spsr;
    kbl::gta9wifi::RebootState reboot;
    volatile uint64_t menu_selected, menu_keys, menu_elapsed_ms;
    volatile int64_t reset_result;
};
static_assert(offsetof(BridgeContext, el) == 80);
static_assert(offsetof(BridgeContext, stage) == 104);
static_assert(offsetof(BridgeContext, fault) == 144);
static_assert(offsetof(BridgeContext, active) == 168);
static_assert(offsetof(BridgeContext, fault_spsr) == 176);
static_assert(offsetof(BridgeContext, reboot) == 184);
static_assert(offsetof(BridgeContext, menu_selected) == 248);
static_assert(offsetof(BridgeContext, reset_result) == 272);
static_assert(sizeof(BridgeContext) == 280);
extern "C" BridgeContext bridge_context;
extern "C" void bridge_clean(uint64_t start, uint64_t bytes);
extern "C" void bridge_menu(BridgeContext*, const kbl::Fdt&, const kbl::gta9wifi::Display&);
extern "C" void bridge_cleanup(BridgeContext*);
extern "C" void bridge_menu_cleanup(BridgeContext*);
extern "C" int64_t bridge_psci_system_reset(uint32_t function);
extern "C" [[noreturn]] void bridge_linux_enter(uint64_t destination,uint64_t source,uint64_t bytes,uint64_t dtb);
