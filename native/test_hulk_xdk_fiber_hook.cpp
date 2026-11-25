#include "hulk_xdk_fiber_hook.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace hulkpocket;
namespace {
constexpr std::uint32_t a=0x1000,b=0x1100,tls=0x1200,fs=0x1300;
constexpr std::uint32_t stack_a=0x8000,stack_b=0x9000;
constexpr std::uint32_t kpcr_stack=0x2000,index=3;
struct Fixture {
    XboxFiberScheduler scheduler;
    X86Context ctx{};
    std::unordered_map<std::uint32_t,std::uint32_t> mem;
    std::vector<int> trace;
    HulkXdkFiberLayout layout=kHulkEuFiberLayout;
    std::uint32_t Read(std::uint32_t address) const {
        if ((address&3) || address >= 0x600000) throw std::out_of_range("guest read");
        auto it=mem.find(address);
        return it==mem.end()?0:it->second;
    }
    void Write(std::uint32_t address,std::uint32_t value) {
        if ((address&3) || address>=0x600000) throw std::out_of_range("guest write");
        mem[address]=value;
    }
    void Switch(std::uint32_t next) {
        // Real XDK call argument: ESP+4 is the target fiber record.
        Write(ctx.esp+4,next);
        HulkXdkSwitchToFiber(scheduler,ctx,layout,
            [this](std::uint32_t at){return Read(at);},
            [this](std::uint32_t at,std::uint32_t v){Write(at,v);});
    }
};
void A(void* raw) {
    auto& t=*static_cast<Fixture*>(raw);
    t.trace.push_back(1);
    t.Switch(b);
    t.trace.push_back(3);
    if(t.Read(tls+0x10)!=a || t.ctx.ebx!=0x2001 ||
       t.ctx.ebp!=0x2004 || t.Read(fs)!=0x7777 ||
       t.ctx.esp!=stack_a+8) std::abort();
    t.Switch(b);
    t.trace.push_back(9);
}
void B(void* raw) {
    auto& t=*static_cast<Fixture*>(raw);
    t.trace.push_back(2);
    if(t.Read(tls+0x10)!=b || t.ctx.ebx!=0x1001 ||
       t.ctx.ebp!=0x1004 || t.Read(fs)!=0x5555 ||
       t.ctx.esp!=stack_b+28) std::abort();
    t.Write(stack_a,0x50EBD);
    t.Write(stack_a+4,0xBEEF);
    t.Switch(a);
    t.trace.push_back(4);
    if(t.Read(tls+0x10)!=b || t.ctx.ebx!=0x1001 ||
       t.ctx.ebp!=0x1004 || t.Read(fs)!=0x5555 ||
       t.ctx.esp!=stack_b+36) std::abort();
    t.scheduler.YieldToHost();
    t.trace.push_back(5);
}
}
int main(){
    Fixture t;
    t.ctx.fs_base=fs;
    t.ctx.esp=stack_a;
    t.ctx.ebx=0x2001; t.ctx.edi=0x2002;
    t.ctx.esi=0x2003; t.ctx.ebp=0x2004;
    t.Write(fs,0x7777);
    t.Write(fs+4,kpcr_stack);
    t.Write(t.layout.fiber_tls_index_va,index);
    t.Write(kpcr_stack+index*4,tls);
    t.Write(tls+0x10,a);
    const std::array<std::uint32_t,7> frame={
        0x5555,0x1001,0x1002,0x1003,0x1004,0x50DD9,0xDEAD};
    for(std::size_t i=0;i<frame.size();++i)
        t.Write(stack_b+std::uint32_t(i*4),frame[i]);
    t.Write(b+0x0C,stack_b);
    t.scheduler.Create(a,A,&t);
    t.scheduler.Create(b,B,&t);
    t.scheduler.Run(a);
    if(t.trace!=std::vector<int>({1,2,3,4}) ||
       t.Read(tls+0x10)!=b || t.scheduler.Finished(b)) return 1;
    t.scheduler.Run(b);
    if(t.trace!=std::vector<int>({1,2,3,4,5}) ||
       !t.scheduler.Finished(b)) return 2;

    // The two regional function addresses are distinct. Reject a
    // trampoline without real guest execution rather than pretending
    // a non-existent fiber is supported.
    if(kHulkUsaFiberLayout.fiber_tls_index_va==kHulkEuFiberLayout.fiber_tls_index_va ||
       kHulkUsaFiberLayout.bootstrap_va==kHulkEuFiberLayout.bootstrap_va)
        return 3;
    std::puts("Hulk XDK 5849 TLS chain + genuine SDK X86Context + ARM64 switch: passed");
    return 0;
}
