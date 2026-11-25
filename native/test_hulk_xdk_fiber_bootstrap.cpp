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
struct Fixture {
    XboxFiberScheduler scheduler;
    X86Context ctx{};
    std::unordered_map<std::uint32_t,std::uint32_t> memory;
    std::vector<int> trace;
    HulkXdkFiberLayout layout=kHulkEuFiberLayout;
    XbeGuestFiberBootstrap bootstrap;
    Fixture() : bootstrap(scheduler, ctx, reinterpret_cast<std::uint8_t*>(this), GuestCall) {}
    static void GuestCall(X86Context& context, std::uint8_t* base, std::uint32_t entry) {
        auto& t=*reinterpret_cast<Fixture*>(base);
        if (&context!=&t.ctx || entry!=t.layout.bootstrap_va ||
            t.scheduler.CurrentId()!=b || t.Read(tls+0x10)!=b ||
            context.ebx!=0x1001 || context.ebp!=0x1004)
            std::abort();
        t.trace.push_back(2);
        // The real SDK x86_icall would execute guest instructions on this
        // native stack and reach the hooked Xbox SwitchToFiber later.
        t.Write(stack_a,0x50EBD);
        t.Write(stack_a+4,0xBEEF);
        t.Switch(a);
        t.trace.push_back(4);
        if(t.Read(tls+0x10)!=b || context.ebx!=0x1001 ||
           context.ebp!=0x1004 || context.esp!=stack_b+36)
            std::abort();
        // Returning from an Xbox bootstrap is invalid; the adapter
        // records the exceptional return for the production trap.
    }
    std::uint32_t Read(std::uint32_t at) const {
        if((at&3)||at>=0x600000) throw std::out_of_range("guest read");
        auto it=memory.find(at);
        return it==memory.end()?0:it->second;
    }
    void Write(std::uint32_t at,std::uint32_t val) {
        if((at&3)||at>=0x600000) throw std::out_of_range("guest write");
        memory[at]=val;
    }
    void Switch(std::uint32_t target) {
        Write(ctx.esp+4,target);
        HulkXdkSwitchToFiber(scheduler,ctx,layout,
            [this](std::uint32_t at){return Read(at);},
            [this](std::uint32_t at,std::uint32_t v){Write(at,v);},
            &bootstrap);
    }
};
void Root(void* ptr) {
    auto& t=*static_cast<Fixture*>(ptr);
    t.trace.push_back(1);
    t.Switch(b); // Creates the ARM64 stack and starts GuestCall.
    t.trace.push_back(3);
    if(t.ctx.ebx!=0x2001 || t.ctx.ebp!=0x2004 ||
       t.ctx.esp!=stack_a+8 || t.Read(tls+0x10)!=a)
        std::abort();
    t.Switch(b); // Resumes the original GuestCall, not a new invocation.
    t.trace.push_back(5);
}
} // namespace

int main(){
    Fixture t;
    t.ctx.fs_base=fs; t.ctx.esp=stack_a;
    t.ctx.ebx=0x2001; t.ctx.edi=0x2002;
    t.ctx.esi=0x2003; t.ctx.ebp=0x2004;
    t.Write(fs,0x7777);
    t.Write(fs+4,0x2000);
    t.Write(t.layout.fiber_tls_index_va,3);
    t.Write(0x2000+12,tls);
    t.Write(tls+0x10,a);
    const std::array<std::uint32_t,7> frame={
        0x5555,0x1001,0x1002,0x1003,0x1004,t.layout.bootstrap_va,0xDEAD};
    for(std::size_t i=0;i<frame.size();++i)
        t.Write(stack_b+std::uint32_t(i*4),frame[i]);
    t.Write(b+0x0C,stack_b);
    t.scheduler.Create(a,Root,&t);
    t.scheduler.Run(a);
    if(t.trace!=std::vector<int>({1,2,3,4}) ||
       !t.bootstrap.UnexpectedReturn(b) || !t.scheduler.Finished(b) ||
       t.scheduler.Finished(a) || t.Read(tls+0x10)!=b)
        return 1;
    // A remained suspended when B's guest trampoline returned.
    // A can resume from its own unchanged native C++ stack.
    t.scheduler.Run(a);
    if(t.trace!=std::vector<int>({1,2,3,4,5}) || !t.scheduler.Finished(a))
        return 2;
    std::puts("Hulk lazy Xbox bootstrap -> SDK guest-call callback -> native A-B-A-B: passed");
    return 0;
}
