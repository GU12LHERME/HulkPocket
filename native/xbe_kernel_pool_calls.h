#pragma once
#include "xbe_kernel_pool.h"
#include "cpu/x86_context.h"
namespace hulkpocket {
// Direct kernel thunk entry: stack contains return address then arguments.
inline bool DispatchPoolCall(uint32_t ordinal,X86Context& cpu,
    XboxGuestAddressSpace& mem,XbeKernelPool& pool) {
    uint32_t args;
    switch(ordinal) {
        case 14: case 17: args=4; break;
        case 15: args=8; break;
        default: return false;
    }
    if(uint64_t(cpu.esp)+4+args>=XboxGuestAddressSpace::kSize ||
       !mem.IsCommitted(cpu.esp,4+args))
        throw std::runtime_error("Invalid pool call stack");
    uint32_t first=mem.Read32(cpu.esp+4);
    if(ordinal==17) pool.Free(first);
    else cpu.eax=pool.Allocate(first);
    cpu.esp+=4+args;
    return true;
}
}
