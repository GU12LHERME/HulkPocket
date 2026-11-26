#pragma once
#include "cpu/x86_context.h"
#include <chrono>
#include <cstdint>
namespace hulkpocket {
inline void GuestCpuId(X86Context& ctx) {
    // Match the SDK's historical virtual Xbox CPU feature response, not
    // the AArch64 host's actual instruction set.
    switch (ctx.eax) {
        case 0:
            ctx.eax = 2u;
            ctx.ebx = 0x756E6547u;
            ctx.edx = 0x49656E69u;
            ctx.ecx = 0x6C65746Eu;
            break;
        case 1:
            ctx.eax = 0x0000068Au;
            ctx.ebx = ctx.ecx = 0u;
            ctx.edx = 0x0383F9FFu;
            break;
        default:
            ctx.eax = ctx.ebx = ctx.ecx = ctx.edx = 0u;
            break;
    }
}

inline std::uint64_t GuestTimestamp() {
    // The SDK assumes 733.33 MHz (virtual Xbox CPU) and a monotonic
    // epoch; avoid floating point rounding or signed overflow.
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    const auto duration = clock::now() - start;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    const auto ticks = static_cast<std::uint64_t>(ns > 0 ? ns : 0);
    const std::uint64_t sec = ticks / 1000000000u;
    const std::uint64_t rem = ticks % 1000000000u;
    return sec * 733333333u + rem * 733333333u / 1000000000u;
}

}
