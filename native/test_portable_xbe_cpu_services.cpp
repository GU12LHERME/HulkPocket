#include "portable_xbe_cpu_services.h"
#include "portable_xbe_dispatch.h"
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace {
void Kernel(X86Context& ctx, std::uint8_t*, std::uint32_t ordinal) {
    if (ordinal != 17) throw std::runtime_error("unexpected kernel ordinal");
    ctx.eax = 0xBEEF;
}
void Entry(X86Context& ctx, std::uint8_t*) {
    x86_cpuid(ctx);
}
}
int main() {
    X86Context ctx{};
    std::uint8_t guest[] = {0};
    ctx.eax = 0;
    x86_cpuid(ctx);
    if (ctx.eax != 2u || ctx.ebx != 0x756E6547u ||
        ctx.edx != 0x49656E69u || ctx.ecx != 0x6C65746Eu) return 1;
    ctx.eax = 1;
    x86_cpuid(ctx);
    if (ctx.eax != 0x68Au || ctx.edx != 0x0383F9FFu) return 2;
    ctx.eax = 0xFFFFFFFFu;
    x86_cpuid(ctx);
    if (ctx.eax || ctx.ebx || ctx.ecx || ctx.edx) return 3;

    auto t1 = x86_rdtsc();
    auto t2 = x86_rdtsc();
    if (t2 < t1) return 4;

    const X86FuncMapping mappings[] = {{0x200000u,Entry}};
    hulkpocket::PortableXbeDispatch dispatcher(
        mappings,1,0x200000u,0x201000u,Kernel);
    bool rejected=false;
    try { x86_kernel(ctx,guest,17); }
    catch(const std::logic_error&) { rejected=true; }
    if (!rejected) return 5;
    {
        hulkpocket::ScopedXbeDispatch scope(dispatcher);
        x86_kernel(ctx,guest,17);
        if(ctx.eax != 0xBEEFu || ctx.last_icall != 0xFE000044u) return 6;
        rejected=false;
        try { x86_kernel(ctx,guest,400); }
        catch(const std::out_of_range&) { rejected=true; }
        if(!rejected) return 7;
        ctx.eax=0;
        x86_icall(ctx,guest,0x200000u);
        if(ctx.ebx != 0x756E6547u) return 8;
    }
    x86_priv(ctx,0x200000u,"cli");
    if(x86_trace(ctx,0x200000u,0x1234u)!=0x1234u) return 9;
    rejected=false;
    try { x86_trace(ctx,0x200000u,0); }
    catch(const std::logic_error&) { rejected=true; }
    if(!rejected) return 10;
    rejected=false;
    try { x86_trap(ctx,0x200000u,"unimplemented"); }
    catch(const std::runtime_error&) { rejected=true; }
    if(!rejected) return 11;
    rejected=false;
    try { x86_resume(ctx,guest,X86Resume{0x200000u,0x9000u}); }
    catch(const std::logic_error&) { rejected=true; }
    if(!rejected) return 12;
    std::puts("Portable Xbox CPU CPUID/TSC/kernel/trap guest ABI: passed");
    return 0;
}
