#pragma once
#include "xbe_guest_fiber_bootstrap.h"
#include "cpu/x86_context.h"
#include <cstdint>
#include <memory>

// Same C++ function signature and symbol used by the upstream
// XboxXBERecomp runtime/cpu/dispatch.cpp. Linking this adapter against
// the real Android runtime will REQUIRE a real implementation of
// x86_icall; Android cannot link the current Win32 dispatch.cpp.
void x86_icall(X86Context& ctx, std::uint8_t* base, std::uint32_t target);

namespace hulkpocket {
inline std::unique_ptr<XbeGuestFiberBootstrap> BindSdkIcall(
    XboxFiberScheduler& scheduler, X86Context& ctx, std::uint8_t* base) {
    return std::make_unique<XbeGuestFiberBootstrap>(
        scheduler, ctx, base, &::x86_icall);
}
}
