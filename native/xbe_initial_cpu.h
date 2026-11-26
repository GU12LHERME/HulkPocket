#pragma once
#include "cpu/x86_context.h"
#include "xbe_bootstrap.h"
#include <stdexcept>
namespace hulkpocket {
// Diagnostic entry context, not a complete Xbox thread implementation.
// Caller supplies a prepared KPCR and a valid runtime thread handle.
inline X86Context PrepareInitialCpu(const XbeBootstrapState& boot,
    XboxGuestAddressSpace& mem, uint32_t kpcr, uint32_t thread_handle,
    uint32_t return_address) {
    if (!kpcr || !thread_handle || !mem.IsCommitted(kpcr,4))
        throw std::invalid_argument("Missing guest thread context");
    if (boot.initial_esp < 4 ||
        boot.initial_esp-4 < boot.memory.stack_base ||
        !mem.IsCommitted(boot.initial_esp-4,4))
        throw std::invalid_argument("Invalid initial stack");
    X86Context ctx{};
    ctx.esp=boot.initial_esp-4;
    ctx.fs_base=kpcr;
    ctx.thread_handle=thread_handle;
    ctx.fcw=0x037f;
    ctx.mxcsr=0x1f80;
    mem.Write32(ctx.esp,return_address);
    return ctx;
}
}
