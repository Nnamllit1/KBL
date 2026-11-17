#include "controls.hpp"
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

using namespace kbl;
using namespace kbl::gta9wifi;
static void check(bool result, const char* why) {
    if (!result) { std::cerr << why << '\n'; std::exit(1); }
}
struct Bus {
    uint32_t matrix=0xffff, status=10, fsm=0, mode=0x17, magic=0xa5a50000;
    uint32_t upload=0x66262564,power_flag=0,reason=0xcafebabe;
    bool busy=false, fail_data=false, reject_mode=false, reject_stop=false;
    size_t reads=0, operations=0, fail_at=0, reset_calls=0;
    uint32_t expected_magic=0xa5a50002, expected_reason=0x12345674, expected_stopped=0x22000016;
    int64_t firmware_result=-1;
    std::vector<std::pair<uint64_t,uint32_t>> writes;
    static bool read(void* data, uint64_t address, uint32_t& out) {
        auto& b=*static_cast<Bus*>(data);++b.reads;
        if(++b.operations==b.fail_at) return false;
        if (address==0x10010004) out=b.matrix;
        else if (address==0x100268a8) out=b.busy?4:b.fsm<<1;
        else if (address==0x10026894) { if(b.fail_data)return false;out=b.status; }
        else if (address==0x10007000) out=b.mode;
        else if (address==0x10007024) out=b.magic;
        else if (address==0x11ffe0) out=b.upload;
        else if (address==0x11ffe8) out=b.power_flag;
        else if (address==0x11ffec) out=b.reason;
        else return false;
        return true;
    }
    static bool write(void* data, uint64_t address, uint32_t value) {
        auto& b=*static_cast<Bus*>(data);
        if(++b.operations==b.fail_at) return false;
        b.writes.emplace_back(address,value);
        if (address==0x10026880 && value==0x28) b.fsm=6;
        else if (address==0x100268a4 && value==1) b.fsm=0;
        else if (address==0x10007000) {if(!b.reject_stop)b.mode=value;}
        else if (address==0x10007024) { if(!b.reject_mode)b.magic=value; }
        else if (address==0x11ffe0) b.upload=value;
        else if (address==0x11ffe8) b.power_flag=value;
        else if (address==0x11ffec) b.reason=value;
        else if (address!=0x10007008) {check(false,"unexpected register write, including forbidden direct SWRST");return false;}
        return true;
    }
    static int64_t reset(void* data, uint32_t function) {
        auto& b=*static_cast<Bus*>(data);++b.reset_calls;
        check(function==0x84000009,"wrong PSCI function");
        check(b.mode==b.expected_stopped && !(b.mode&1),"firmware called before stopping watchdog");
        check(b.magic==b.expected_magic && b.upload==0 && b.power_flag==0x12345678 && b.reason==b.expected_reason,
              "firmware called before preparing correct restart markers");
        return b.firmware_result;
    }
    RegisterIO io() { return {this,read,write}; }
    FirmwareReset firmware() {return {this,reset};}
};
int main() {
    Controls c{0x10010000,0x10026000,0x10007000,1,2,0,0x11ffe0,true};
    for (uint8_t mask=0;mask<8;++mask) {
        Bus b{};b.matrix=(mask&volume_up)?0xfffd:0xffff;
        b.status=((mask&power)?0u:2u)|((mask&volume_down)?0u:8u);
        uint8_t found=0xff;
        check(sample_buttons(c,b.io(),found)&&found==mask,"incorrect physical button decode");
        check(b.writes==std::vector<std::pair<uint64_t,uint32_t>>{{0x10026880,0x28},{0x100268a4,1}},
              "PMIC polling performed an unexpected register write");
        check(b.fsm==0,"PMIC read left pending data");
    }
    Bus busy{};busy.busy=true;uint8_t unchanged=0x7f;
    check(!sample_buttons(c,busy.io(),unchanged)&&unchanged==0x7f,"busy PMIC yielded a key");
    check(busy.reads<=10001&&busy.writes.empty(),"busy polling was unbounded or issued a command");
    Bus failed{};failed.fail_data=true;
    check(!sample_buttons(c,failed.io(),unchanged)&&failed.fsm==0,"failed PMIC read not acknowledged");
    for (auto action : {Action::recovery,Action::download}) for(uint32_t initial_mode : {0x17u,0x5fu,0u}) {
        Bus b{};RebootState state{};b.mode=initial_mode;
        b.expected_stopped=(initial_mode&~1u)|0x22000000u;
        b.expected_magic=0xa5a50000u|(action==Action::recovery?2u:0u);
        b.expected_reason=action==Action::recovery?0x12345674u:0x12345671u;
        check(request_reboot(c,b.io(),b.firmware(),action,state)==-1 && b.reset_calls==1,"firmware error not returned");
        check(b.magic==(0xa5a50000u|(action==Action::recovery?2u:0u)),"reset mode damaged unrelated bits");
        check(b.upload==0 && b.power_flag==0x12345678 &&
              b.reason==(action==Action::recovery?0x12345674u:0x12345671u),"Samsung restart markers missing or incorrect");
        check(b.mode==b.expected_stopped,"reset mode damaged watchdog settings");
        check(restore_reboot(b.io(),state)&&!state.dirty,"returned reset not restored");
        check(b.magic==0xa5a50000&&b.mode==(initial_mode|0x22000000u),"reset restoration changed saved state");
        check(b.upload==0x66262564&&b.power_flag==0&&b.reason==0xcafebabe,"Samsung markers not restored after returned reset");
        auto count=b.writes.size();check(restore_reboot(b.io(),state)&&b.writes.size()==count,"restore was not idempotent");
    }
    Bus rejected{};rejected.reject_mode=true;RebootState state{};
    check(request_reboot(c,rejected.io(),rejected.firmware(),Action::recovery,state)==reset_not_prepared &&
          !rejected.reset_calls,"failed reboot-mode readback accepted");
    check(restore_reboot(rejected.io(),state),"failed preparation could not be restored");
    Bus download{};download.magic=0xa5a50009;RebootState download_state{};
    download.expected_magic=download.magic;download.expected_reason=0x12345671;
    check(request_reboot(c,download.io(),download.firmware(),Action::download,download_state)==-1 && download.magic==0xa5a50009,
          "download changed the stock driver's unrelated syscon mode");
    for(auto write:download.writes) check(write.first!=0x10007024,"download wrote an unspecified syscon mode");
    check(restore_reboot(download.io(),download_state),"download restoration failed");
    Bus invalid{};RebootState unused{};
    check(request_reboot(c,invalid.io(),invalid.firmware(),Action::android,unused)==reset_not_prepared &&
          !invalid.operations && !invalid.reset_calls,"Android caused reset operations");
    Controls unsupported=c;unsupported.psci_smc=false;
    check(request_reboot(unsupported,invalid.io(),invalid.firmware(),Action::recovery,unused)==reset_not_prepared &&
          !invalid.operations && !invalid.reset_calls,"unverified firmware conduit caused reset operations");
    check(request_reboot(c,invalid.io(),{nullptr,nullptr},Action::recovery,unused)==reset_not_prepared &&
          !invalid.operations,"missing firmware callback caused register operations");
    Bus running{};running.reject_stop=true;RebootState stop_state{};
    check(request_reboot(c,running.io(),running.firmware(),Action::recovery,stop_state)==reset_not_prepared &&
          !running.reset_calls,"firmware called with watchdog still running");
    check(restore_reboot(running.io(),stop_state),"failed watchdog stop left markers changed");
    Bus baseline{};RebootState baseline_state{};
    request_reboot(c,baseline.io(),baseline.firmware(),Action::recovery,baseline_state);
    for(size_t operation=1;operation<=baseline.operations;++operation) {
        Bus failure{};failure.fail_at=operation;RebootState saved{};
        check(request_reboot(c,failure.io(),failure.firmware(),Action::recovery,saved)==reset_not_prepared &&
              !failure.reset_calls,"firmware reset after failed preparation IO");
        check(restore_reboot(failure.io(),saved) && !saved.dirty,"failed preparation not recoverable");
        check((failure.mode&0xffffffu)==0x17 && failure.magic==0xa5a50000 && failure.upload==0x66262564 &&
              failure.power_flag==0 && failure.reason==0xcafebabe,"failed preparation left changed registers");
    }
    for(int64_t result : {int64_t(0),int64_t(-3),int64_t(-9)}) {
        Bus returned{};returned.firmware_result=result;RebootState saved{};
        check(request_reboot(c,returned.io(),returned.firmware(),Action::recovery,saved)==result,
              "firmware status was not preserved");
        check(restore_reboot(returned.io(),saved),"returned firmware reset could not be restored");
    }
    Bus linux{};RebootState linux_state{};
    check(stop_watchdog(c,linux.io(),linux_state) && linux.mode==0x22000016 && linux_state.dirty,
          "independent-kernel watchdog stop failed");
    check(linux.writes==std::vector<std::pair<uint64_t,uint32_t>>{{0x10007000,0x22000016}},
          "Linux preparation wrote a mode marker or reset trigger");
    check(restore_reboot(linux.io(),linux_state) && linux.mode==0x22000017,
          "failed Linux preparation could not restore watchdog");
    check(linux.upload==0x66262564 && linux.power_flag==0 && linux.reason==0xcafebabe,
          "Linux preparation modified Samsung restart markers");
    Buttons buttons{};
    check(buttons.update(power,0)==Key::none,"initial carried Power accepted");
    check(buttons.update(power,500)==Key::none,"held Power accepted");
    check(buttons.update(0,510)==Key::none,"release selected a boot action");
    check(buttons.update(0,600)==Key::none&&buttons.armed,"initial release not armed");
    check(buttons.update(volume_down,610)==Key::none,"unfiltered button accepted");
    check(buttons.update(volume_down,645)==Key::cancel,"press did not stop countdown");
    check(buttons.update(0,650)==Key::none,"release was not debounced");
    check(buttons.update(0,685)==Key::down,"Volume Down release lost");
    buttons.update(power,700);check(buttons.update(power,735)==Key::cancel,"Power press did not pause");
    buttons.update(0,740);check(buttons.update(0,775)==Key::select,"Power release not selected");
    buttons.update(power|volume_up,800);buttons.update(power|volume_up,835);
    buttons.update(power,840);buttons.update(power,875);
    buttons.update(0,880);check(buttons.update(0,915)==Key::none,"chord release selected an action");
    Buttons bouncing{};bouncing.update(0,0);bouncing.update(0,100);
    bouncing.update(power,110);bouncing.update(0,120);
    check(bouncing.update(0,160)==Key::none,"button bounce selected an action");
    Config config{};Error error{};
    check(parse_config(text("timeout=8\nentry=a|ANDROID|android|||\nentry=r|RECOVERY|recovery|||\n"),config,error),"Android config rejected");
    check(!parse_config(text("timeout=8\nentry=r|RECOVERY|recovery|||\n"),config,error),"recovery allowed automatic reset");
    std::cout<<"Buttons, PMIC read-only commands, bounded polling and reset rollback passed\n";
}
