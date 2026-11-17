#pragma once
#include "kbl/buttons.hpp"
#include "kbl/fdt.hpp"

namespace kbl::gta9wifi {
struct Controls {
    uint64_t keypad = 0, pwrap = 0, watchdog = 0;
    uint32_t volume_up_bit = 0, recovery_mode = 0, download_mode = 0;
    uint64_t samsung_reset = 0;
    bool psci_smc = false;
};
struct RegisterIO {
    void* context;
    bool (*read)(void*, uint64_t, uint32_t&);
    bool (*write)(void*, uint64_t, uint32_t);
    bool read32(uint64_t address, uint32_t& value) const { return read(context, address, value); }
    bool write32(uint64_t address, uint32_t value) const { return write(context, address, value); }
};
bool discover_controls(const Fdt& tree, Controls& out, const char*& error);
// PWRAP command writes request only a read of PMIC TOPSTATUS; no PMIC register
// is written or configured. Busy polling has a fixed upper bound.
bool sample_buttons(const Controls& controls, const RegisterIO& io, uint8_t& buttons);
bool kick_watchdog(const Controls& controls, const RegisterIO& io);
struct RebootState {
    volatile uint64_t base = 0, old_mode = 0, old_magic = 0, dirty = 0;
    volatile uint64_t samsung = 0, old_upload = 0, old_power = 0, old_reason = 0;
};
constexpr uint32_t psci_system_reset = 0x84000009;
constexpr int64_t reset_not_prepared = -65536;
struct FirmwareReset {
    void* context;
    int64_t (*smc)(void*, uint32_t function);
};
// Prepare Samsung's mode markers, stop the watchdog, then request PSCI reset.
// Successful firmware reset never returns. On any return or caught exception,
// the caller must restore the saved registers before continuing Android.
int64_t request_reboot(const Controls&, const RegisterIO&, const FirmwareReset&, Action, RebootState&);
// Stop the inherited watchdog before handing control to an independent kernel.
// The snapshot can be restored if preparation fails before committing the copy.
bool stop_watchdog(const Controls&, const RegisterIO&, RebootState&);
bool restore_reboot(const RegisterIO&, RebootState&);
} // namespace kbl::gta9wifi
