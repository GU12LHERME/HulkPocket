#include "cpu/x86_context.h"
#include <cstdint>

// Asset-free dummy recompiled Xbox function which actually dereferences
// guest virtual memory at base + an address loaded from the X86Context.
namespace {
void SyntheticXboxEntry(X86Context& ctx,std::uint8_t* base) {
    const auto a=ctx.ecx;
    ctx.eax = std::uint32_t(base[a]) |
             (std::uint32_t(base[a+1]) << 8) |
             (std::uint32_t(base[a+2]) << 16) |
             (std::uint32_t(base[a+3]) << 24);
}
}
extern "C" {
__attribute__((visibility("default"))) extern const std::uint32_t X86CodeBase=0x300000;
__attribute__((visibility("default"))) extern const std::uint32_t X86CodeEnd=0x301000;
__attribute__((visibility("default"))) extern const std::uint32_t X86EntryPoint=0x300010;
__attribute__((visibility("default"))) extern const X86FuncMapping X86FuncMappings[]={
    {0x300010,SyntheticXboxEntry},
    {0,nullptr}
};
}
