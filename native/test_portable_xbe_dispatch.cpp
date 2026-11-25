#include "portable_xbe_dispatch.h"
#include "xbe_sdk_icall_binding.h"
#include "hulk_xdk_fiber_hook.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
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
    std::unordered_map<std::uint32_t,std::uint32_t> mem;
    std::vector<int> trace;
    HulkXdkFiberLayout layout=kHulkEuFiberLayout;
    std::unique_ptr<XbeGuestFiberBootstrap> bootstrap;
    Fixture() : bootstrap(BindSdkIcall(scheduler,ctx,reinterpret_cast<std::uint8_t*>(this))) {}
    std::uint32_t Read(std::uint32_t at) const {
        if ((at&3)||at>=0x600000) throw std::out_of_range("guest read");
        const auto it=mem.find(at); return it==mem.end()?0:it->second;
    }
    void Write(std::uint32_t at,std::uint32_t v) {
        if ((at&3)||at>=0x600000) throw std::out_of_range("guest write");
        mem[at]=v;
    }
    void Switch(std::uint32_t target) {
        Write(ctx.esp+4,target);
        HulkXdkSwitchToFiber(scheduler,ctx,layout,
            [this](std::uint32_t at){return Read(at);},
            [this](std::uint32_t at,std::uint32_t v){Write(at,v);},
            bootstrap.get());
    }
};
void RecompiledGuestFunction(X86Context& ctx,std::uint8_t* base) {
    auto& t=*reinterpret_cast<Fixture*>(base);
    if (&ctx!=&t.ctx || t.scheduler.CurrentId()!=b ||
        t.Read(tls+0x10)!=b || ctx.ebx!=0x1001 ||
        ctx.esp!=stack_b+28 || ctx.last_icall!=t.layout.bootstrap_va)
        std::abort();
    t.trace.push_back(2);
    t.Write(stack_a,0x50EBD);
    t.Write(stack_a+4,0xBEEF);
    t.Switch(a);
    t.trace.push_back(4);
    if(t.scheduler.CurrentId()!=b || t.Read(tls+0x10)!=b ||
       ctx.ebx!=0x1001 || ctx.esp!=stack_b+36)
        std::abort();
}
void Root(void* raw) {
    auto& t=*static_cast<Fixture*>(raw);
    t.trace.push_back(1);
    t.Switch(b);
    t.trace.push_back(3);
    if(t.scheduler.CurrentId()!=a || t.Read(tls+0x10)!=a ||
       t.ctx.ebx!=0x2001 || t.ctx.ebp!=0x2004 ||
       t.ctx.esp!=stack_a+8) std::abort();
    t.Switch(b);
    t.trace.push_back(9);
}
void AnotherGuest(X86Context& ctx,std::uint8_t*) { ctx.eax=0xCAFE; }
} // namespace

int main() {
    constexpr std::uint32_t lo=0x300000, hi=0x400000;
    const X86FuncMapping mapping[]={
        {kHulkEuFiberLayout.bootstrap_va,RecompiledGuestFunction},
        {0x390000,AnotherGuest}
    };
    PortableXbeDispatch dispatcher(mapping,2,lo,hi);
    if(dispatcher.FunctionCount()!=2 ||
       !dispatcher.Contains(kHulkEuFiberLayout.bootstrap_va) ||
       dispatcher.Contains(0x390001)) return 1;
    X86Context temp{};
    std::uint8_t dummy=0;
    bool rejected=false;
    try { x86_icall(temp,&dummy,0x390000); }
    catch(const std::logic_error&) { rejected=true; }
    if(!rejected) return 2;
    {
        ScopedXbeDispatch bound(dispatcher);
        x86_icall(temp,&dummy,0x390000);
        if(temp.eax!=0xCAFE || temp.last_icall!=0x390000) return 3;
        rejected=false;
        try { x86_icall(temp,&dummy,0x390001); }
        catch(const std::out_of_range&) { rejected=true; }
        if(!rejected) return 4;

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
           !t.bootstrap->UnexpectedReturn(b) ||
           !t.scheduler.Finished(b) || t.scheduler.Finished(a)) return 5;
    }
    rejected=false;
    try { x86_icall(temp,&dummy,0x390000); }
    catch(const std::logic_error&) { rejected=true; }
    if(!rejected) return 6;
    try {
        const X86FuncMapping duplicates[]={
            {0x390000,AnotherGuest},{0x390000,RecompiledGuestFunction}};
        PortableXbeDispatch invalid(duplicates,2,lo,hi);
        return 7;
    } catch (const std::invalid_argument&) {}
    std::puts("Real portable x86_icall -> SDK function map -> native ARM64 fiber: passed");
    return 0;
}
