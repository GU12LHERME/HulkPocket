#include "hulk_xdk_fiber_hook.h"
#include "xbox_guest_address_space.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace hulkpocket;
namespace {
constexpr std::uint32_t a=0x10000,b=0x10100,tls=0x10200,fs=0x10300;
constexpr std::uint32_t stack_a=0x30000,stack_b=0x40000;
constexpr std::uint32_t kpcr=0x20000;
struct Fixture {
    XboxGuestAddressSpace memory;
    XboxFiberScheduler scheduler;
    X86Context ctx{};
    std::vector<int> trace;
    std::unique_ptr<XbeGuestFiberBootstrap> bootstrap;
    Fixture() {
        memory.Commit(0x10000,0x2000);
        memory.Commit(kpcr,0x1000);
        memory.Commit(stack_a,0x2000);
        memory.Commit(stack_b,0x2000);
        memory.Commit(kHulkEuFiberLayout.fiber_tls_index_va,4);
        ctx.host=this;
        ctx.fs_base=fs;
        ctx.esp=stack_a+0x1000;
        ctx.ebx=0x2001;ctx.edi=0x2002;
        ctx.esi=0x2003;ctx.ebp=0x2004;
        memory.Write32(fs,0x7777);
        memory.Write32(fs+4,kpcr);
        memory.Write32(kHulkEuFiberLayout.fiber_tls_index_va,3);
        memory.Write32(kpcr+12,tls);
        memory.Write32(tls+0x10,a);
        const std::array<std::uint32_t,7> frame={
            0x5555,0x1001,0x1002,0x1003,0x1004,
            kHulkEuFiberLayout.bootstrap_va,0xDEAD};
        for(std::size_t i=0;i<frame.size();++i)
            memory.Write32(stack_b+std::uint32_t(i*4),frame[i]);
        memory.Write32(b+0x0C,stack_b);
        bootstrap=std::make_unique<XbeGuestFiberBootstrap>(
            scheduler,ctx,memory.Base(),GuestCall);
    }
    static void GuestCall(X86Context& ctx,std::uint8_t* base,std::uint32_t entry) {
        auto& t=*static_cast<Fixture*>(ctx.host);
        if(base!=t.memory.Base() || entry!=kHulkEuFiberLayout.bootstrap_va ||
           t.scheduler.CurrentId()!=b || ctx.ebx!=0x1001 ||
           t.memory.Read32(tls+0x10)!=b || ctx.esp!=stack_b+28)
            std::abort();
        t.trace.push_back(2);
        t.memory.Write32(stack_a+0x1000,0x50EBD);
        t.memory.Write32(stack_a+0x1004,0xBEEF);
        t.Switch(a);
        t.trace.push_back(4);
        if(ctx.ebx!=0x1001 || t.memory.Read32(tls+0x10)!=b)
            std::abort();
    }
    void Switch(std::uint32_t next) {
        memory.Write32(ctx.esp+4,next);
        HulkXdkSwitchToFiber(
            scheduler,ctx,kHulkEuFiberLayout,
            [this](std::uint32_t va){return memory.Read32(va);},
            [this](std::uint32_t va,std::uint32_t v){memory.Write32(va,v);},
            bootstrap.get());
    }
};
void Root(void* opaque) {
    auto& t=*static_cast<Fixture*>(opaque);
    t.trace.push_back(1);
    t.Switch(b);
    t.trace.push_back(3);
    if(t.memory.Read32(tls+0x10)!=a || t.ctx.ebx!=0x2001 ||
       t.ctx.esp!=stack_a+0x1000+8)
        std::abort();
    t.Switch(b);
    t.trace.push_back(9);
}
} // namespace

int main() {
    Fixture fixture;
    fixture.scheduler.Create(a,Root,&fixture);
    fixture.scheduler.Run(a);
    if(fixture.trace!=std::vector<int>({1,2,3,4}) ||
       !fixture.bootstrap->UnexpectedReturn(b) ||
       !fixture.scheduler.Finished(b) ||
       fixture.scheduler.Finished(a))
        return 1;
    std::puts("Real sparse Xbox memory + ARM64 fibers + SDK guest stack: passed");
    return 0;
}
