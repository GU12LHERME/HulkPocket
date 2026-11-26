#include "portable_xbe_cpu_services.h"
#include <cassert>
#include <stdexcept>
int main() {
    X86Context ctx{};
    x86_cpuid(ctx);
    assert(ctx.eax==2 && ctx.ebx==0x756e6547u);
    ctx.eax=1;x86_cpuid(ctx);
    assert(ctx.eax==0x68a && ctx.edx==0x0383f9ffu);
    ctx.eax=0xffffffffu;x86_cpuid(ctx);
    assert(!ctx.eax && !ctx.ebx && !ctx.ecx && !ctx.edx);
    auto t=x86_rdtsc();assert(x86_rdtsc()>=t);
    bool stopped=false;
    try {x86_priv(ctx,0x200000,"cli");} catch(const std::runtime_error&) {stopped=true;}
    assert(stopped);
}
