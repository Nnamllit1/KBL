#include "controls.hpp"

namespace kbl::gta9wifi {
namespace {
bool word(const Fdt& tree, const char* path, const char* name, uint32_t expected) {
    Bytes value{};
    return tree.property(text(path), text(name), value) && value.size == 4 && big32(value.data) == expected;
}
bool compatible(const Fdt& tree, const char* path, const char* name) {
    Bytes value{};
    if (!tree.property(text(path), text("compatible"), value)) return false;
    size_t begin = 0;
    for (size_t i = 0; i < value.size; ++i) if (!value.data[i]) {
        if (equal({reinterpret_cast<const char*>(value.data + begin), i - begin}, text(name))) return true;
        begin = i + 1;
    }
    return false;
}
bool reg(const Fdt& tree, const char* path, uint64_t expected, uint64_t minimum) {
    Bytes value{};
    return tree.property(text(path), text("reg"), value) && value.size == 16 &&
        big64(value.data) == expected && big64(value.data + 8) >= minimum;
}
bool enabled(const Fdt& tree, const char* path) {
    Bytes value{};
    if (!tree.property(text(path),text("status"),value)) return true;
    const Text status{reinterpret_cast<const char*>(value.data),value.size};
    return equal(status,Text{"okay",5}) || equal(status,Text{"ok",3});
}
bool wait_state(const RegisterIO& io, uint64_t pwrap, unsigned state) {
    for (unsigned i = 0; i < 10000; ++i) {
        uint32_t value = 0;
        if (!io.read32(pwrap + 0x8a8, value)) return false;
        if (((value >> 1) & 7) == state) return true;
    }
    return false;
}
}
bool discover_controls(const Fdt& tree, Controls& out, const char*& error) {
    error = "unsupported control layout";
    constexpr auto kp = "/soc/kp@10010000";
    constexpr auto pw = "/soc/pwrap@10026000";
    constexpr auto keys = "/soc/pwrap@10026000/mt6366/mt6366keys";
    constexpr auto wd = "/soc/watchdog@10007000";
    constexpr auto mode = "/soc/watchdog@10007000/reboot-mode";
    Bytes map{};
    if (!enabled(tree,kp) || !enabled(tree,pw) || !enabled(tree,keys) || !enabled(tree,wd) ||
        !compatible(tree, kp, "mediatek,kp") || !reg(tree, kp, 0x10010000, 0x20) ||
        !word(tree, kp, "mediatek,hw-map-num", 72) ||
        !tree.property(text(kp), text("mediatek,hw-init-map"), map) || map.size != 72 * 4 ||
        !compatible(tree, pw, "mediatek,mt6789-pwrap") || !reg(tree, pw, 0x10026000, 0x8ac) ||
        !compatible(tree, keys, "mediatek,mt6366-keys") ||
        !word(tree, "/soc/pwrap@10026000/mt6366/mt6366keys/power", "linux,keycodes", 116) ||
        !word(tree, "/soc/pwrap@10026000/mt6366/mt6366keys/home", "linux,keycodes", 114) ||
        !compatible(tree, wd, "mediatek,mt6789-wdt") || !reg(tree, wd, 0x10007000, 0x28) ||
        !compatible(tree, mode, "syscon-reboot-mode") || !word(tree, mode, "offset", 0x24) ||
        !word(tree, mode, "mask", 15) || !word(tree, mode, "mode-recovery", 2) ||
        !word(tree, mode, "mode-bootloader", 3)) return false;
    unsigned found = 0, bit = 0;
    for (unsigned i = 0; i < 72; ++i) if (big32(map.data + i * 4) == 115) { bit = i; ++found; }
    if (found != 1 || bit >= 16) return false;
    Bytes method{};
    if (!enabled(tree,"/psci") ||
        !(compatible(tree,"/psci","arm,psci-1.0") || compatible(tree,"/psci","arm,psci-0.2")) ||
        !tree.property(text("/psci"),text("method"),method)) return false;
    const bool smc=equal({reinterpret_cast<const char*>(method.data),method.size},Text{"smc",4});
#ifdef KBL_MENU_VM_TEST
    // QEMU's real Linux DT can use HVC. The VM-only input/reset bus does not
    // invoke firmware, and tablet packaging rejects every build with this flag.
    if(!smc && !equal({reinterpret_cast<const char*>(method.data),method.size},Text{"hvc",4})) return false;
#else
    if(!smc) return false;
#endif
    // CYE5 sec_reboot/mrdump use Samsung's retained SRAM reason block too.
    // "download" has no syscon mode entry: stock leaves that field alone.
    out = {0x10010000, 0x10026000, 0x10007000, bit, 2, 0, 0x11ffe0, smc};
    error = nullptr;
    return true;
}
bool sample_buttons(const Controls& c, const RegisterIO& io, uint8_t& buttons) {
    uint32_t matrix = 0, status = 0;
    if (!io.read32(c.keypad + 4, matrix) || !wait_state(io, c.pwrap, 0)) return false;
    // MT6789 PWRAP arbitration interface. Operation bit 29 stays clear (read).
    if (!io.write32(c.pwrap + 0x880, 0x28)) return false;
    if (!wait_state(io, c.pwrap, 6)) return false;
    const bool read_ok = io.read32(c.pwrap + 0x894, status);
    const bool ack_ok = io.write32(c.pwrap + 0x8a4, 1);
    if (!read_ok || !ack_ok || !wait_state(io, c.pwrap, 0)) return false;
    uint8_t value = 0;
    if (!(matrix & (1u << c.volume_up_bit))) value |= volume_up;
    if (!(status & 2)) value |= power;
    if (!(status & 8)) value |= volume_down;
    buttons = value;
    return true;
}
bool kick_watchdog(const Controls& c, const RegisterIO& io) {
    return io.write32(c.watchdog + 8, 0x1971);
}
int64_t request_reboot(const Controls& c, const RegisterIO& io, const FirmwareReset& firmware,
                       Action action, RebootState& state) {
    uint32_t mode = 0, magic = 0, upload = 0, power_flag = 0, reason = 0;
    if (state.dirty || (action != Action::recovery && action != Action::download) ||
        c.samsung_reset != 0x11ffe0 || !c.psci_smc || !firmware.smc ||
        !io.read32(c.watchdog, mode) || !io.read32(c.watchdog + 0x24, magic) ||
        !io.read32(c.samsung_reset,upload) || !io.read32(c.samsung_reset+8,power_flag) ||
        !io.read32(c.samsung_reset+12,reason)) return reset_not_prepared;
    state.base = c.watchdog; state.old_mode = mode; state.old_magic = magic;
    state.samsung=c.samsung_reset;state.old_upload=upload;state.old_power=power_flag;state.old_reason=reason;
    state.dirty = 1;
    const auto target = action == Action::recovery ? c.recovery_mode : c.download_mode;
    const uint32_t samsung_reason=action==Action::recovery?0x12345674u:0x12345671u;
    // Match the stock sec_reboot contract. Leave upload_reason and every other
    // SRAM field alone; these three words request a normal software restart.
    if (!io.write32(c.samsung_reset+8,0x12345678) ||
        !io.write32(c.samsung_reset+12,samsung_reason) || !io.write32(c.samsung_reset,0) ||
        !io.read32(c.samsung_reset+8,power_flag) || power_flag!=0x12345678 ||
        !io.read32(c.samsung_reset+12,reason) || reason!=samsung_reason ||
        !io.read32(c.samsung_reset,upload) || upload!=0 ||
        (target && !io.write32(c.watchdog + 0x24, (magic & ~15u) | target)) ||
        !io.read32(c.watchdog + 0x24, magic) ||
        (target ? (magic & 15)!=target : magic!=uint32_t(state.old_magic))) return reset_not_prepared;
    // Match mtk_wdt_stop on the normal kernel reboot path: clear EN only.
    // Direct SWRST bypasses the higher-priority PSCI restart notifier and can
    // enter firmware's watchdog-failure path instead of its requested mode.
    uint32_t stopped = 0;
    if (!io.write32(c.watchdog,(mode & ~1u) | 0x22000000u) ||
        !io.read32(c.watchdog,stopped) ||
        (stopped & 0x00ffffffu) != ((mode & ~1u) & 0x00ffffffu)) return reset_not_prepared;
    return firmware.smc(firmware.context,psci_system_reset);
}
bool stop_watchdog(const Controls& c,const RegisterIO& io,RebootState& state) {
    uint32_t mode=0,magic=0,stopped=0;
    if(state.dirty || !io.read32(c.watchdog,mode) || !io.read32(c.watchdog+0x24,magic)) return false;
    state.base=c.watchdog;state.old_mode=mode;state.old_magic=magic;state.samsung=0;state.dirty=1;
    return io.write32(c.watchdog,(mode&~1u)|0x22000000u) && io.read32(c.watchdog,stopped) &&
        (stopped&0x00ffffffu)==((mode&~1u)&0x00ffffffu);
}
bool restore_reboot(const RegisterIO& io, RebootState& state) {
    if (!state.dirty) return true;
    const auto base = state.base;
    state.dirty = 0; // Do not retry a faulting restoration from the fault handler.
    const bool magic = io.write32(base + 0x24, uint32_t(state.old_magic));
    const bool reason=!state.samsung || io.write32(state.samsung+12,uint32_t(state.old_reason));
    const bool power_flag=!state.samsung || io.write32(state.samsung+8,uint32_t(state.old_power));
    const bool upload=!state.samsung || io.write32(state.samsung,uint32_t(state.old_upload));
    // Restore markers before re-enabling a previously running watchdog.
    const bool mode = io.write32(base, uint32_t(state.old_mode) | 0x22000000u);
    return magic && mode && reason && power_flag && upload;
}
} // namespace kbl::gta9wifi
