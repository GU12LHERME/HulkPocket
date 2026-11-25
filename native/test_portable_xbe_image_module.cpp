#include "cpu/x86_context.h"
#include <cstdint>

namespace {
void GameEntry(X86Context& ctx, std::uint8_t* base) {
    ctx.eax = std::uint32_t(base[0]) + 0x1234;
}
void GameOther(X86Context& ctx, std::uint8_t*) {
    ctx.ebx = 0xCAFE;
}
}
extern "C" {
__attribute__((visibility("default"))) extern const std::uint32_t X86CodeBase=0x300000;
__attribute__((visibility("default"))) extern const std::uint32_t X86CodeEnd=0x310000;
__attribute__((visibility("default"))) extern const std::uint32_t X86EntryPoint=0x300010;
// Exact SDK generated export table shape: guest VA, host function, null.
__attribute__((visibility("default"))) extern const X86FuncMapping X86FuncMappings[]={
    {0x300010,GameEntry},
    {0x300020,GameOther},
    {0,nullptr}
};
}
