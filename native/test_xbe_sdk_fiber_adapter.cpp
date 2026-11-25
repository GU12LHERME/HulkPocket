#include "xbe_sdk_fiber_adapter.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

using namespace hulkpocket;
namespace {
constexpr std::uint32_t a = 0x1000, b = 0x1100, tls = 0x1200, fs = 0x1300;
constexpr std::uint32_t stack_a = 0x8000, stack_b = 0x9000;
struct TestState {
    XboxFiberScheduler scheduler;
    X86Context ctx{};
    std::vector<std::uint32_t> mem = std::vector<std::uint32_t>(65536);
    std::vector<int> seen;
    std::uint32_t Read(std::uint32_t at) const {
        if ((at & 3) || at/4 >= mem.size()) throw std::out_of_range("bad read");
        return mem[at/4];
    }
    void Write(std::uint32_t at, std::uint32_t v) {
        if ((at & 3) || at/4 >= mem.size()) throw std::out_of_range("bad write");
        mem[at/4] = v;
    }
    XboxGuestFiberSwap Switch(std::uint32_t next) {
        XbeSdkFiberAdapter adapter(scheduler, ctx);
        return adapter.SwitchTo(next, tls,
            [this](std::uint32_t at){ return Read(at); },
            [this](std::uint32_t at, std::uint32_t value){ Write(at,value); });
    }
};
void FiberA(void* p) {
    auto& t = *static_cast<TestState*>(p);
    t.seen.push_back(1);
    t.Switch(b);
    t.seen.push_back(3);
    if (t.Read(tls+0x10)!=a || t.ctx.ebx!=0x2001 ||
        t.ctx.ebp!=0x2004 || t.ctx.esp!=stack_a+8 ||
        t.Read(fs)!=0x7777) std::abort();
    t.Switch(b);
    t.seen.push_back(9);
}
void FiberB(void* p) {
    auto& t = *static_cast<TestState*>(p);
    t.seen.push_back(2);
    if(t.Read(tls+0x10)!=b || t.ctx.ebx!=0x1001 ||
       t.ctx.ebp!=0x1004 || t.ctx.esp!=stack_b+28 ||
       t.Read(fs)!=0x5555) std::abort();
    t.Write(stack_a, 0x50EBD);
    t.Write(stack_a+4, 0xBEEF);
    t.Switch(a);
    t.seen.push_back(4);
    if(t.Read(tls+0x10)!=b || t.ctx.ebx!=0x1001 ||
       t.ctx.ebp!=0x1004 || t.ctx.esp!=stack_b+36 ||
       t.Read(fs)!=0x5555) std::abort();
    t.scheduler.YieldToHost();
    t.seen.push_back(5);
}
}
int main(){
    TestState t;
    t.ctx.fs_base=fs;
    t.ctx.esp=stack_a;
    t.ctx.ebx=0x2001; t.ctx.edi=0x2002;
    t.ctx.esi=0x2003; t.ctx.ebp=0x2004;
    t.Write(fs,0x7777);
    t.Write(tls+0x10,a);
    const std::array<std::uint32_t,7> frame={0x5555,0x1001,0x1002,0x1003,0x1004,0x50DD9,0xDEAD};
    for (std::size_t i=0;i<frame.size();++i) t.Write(stack_b+std::uint32_t(4*i),frame[i]);
    t.Write(b+0x0C,stack_b);
    t.scheduler.Create(a,FiberA,&t);
    t.scheduler.Create(b,FiberB,&t);
    t.scheduler.Run(a);
    if(t.seen!=std::vector<int>({1,2,3,4}) || t.scheduler.Finished(b) ||
       t.ctx.ebx!=0x1001 || t.ctx.esp!=stack_b+36) return 1;
    t.scheduler.Run(b);
    if(t.seen!=std::vector<int>({1,2,3,4,5}) || !t.scheduler.Finished(b)) return 2;
    std::puts("Real XboxXBERecomp X86Context + ARM64 A-B-A-B: passed");
    return 0;
}
