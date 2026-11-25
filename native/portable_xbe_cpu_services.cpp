#include "cpu/x86_ops.h"
#include "portable_xbe_dispatch.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

// ABI-compatible subset of XboxXBERecomp's Windows-only dispatch.cpp.
// This contains only guest CPU primitives; Xbox kernel services are
// delivered by PortableXbeDispatch::KernelCall and are NOT emulated here.

void x86_kernel(X86Context& ctx, std::uint8_t* base, std::uint32_t ordinal) {
    if (ordinal >= 400u)
        throw std::out_of_range("Xbox kernel ordinal outside supported thunk range");
    x86_icall(ctx, base, 0xFE000000u + ordinal * 4u);
}

void x86_cpuid(X86Context& ctx) {
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

std::uint64_t x86_rdtsc() {
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

void x86_priv(X86Context&, std::uint32_t, const char*) {
    // The original SDK ignores privileged no-op instructions while
    // logging them. A proper Xbox kernel privilege model is still absent.
}

std::uint32_t x86_trace(X86Context& ctx, std::uint32_t va, std::uint32_t ret) {
    (void)va;
    // Match SDK trace's return-address contract without logging private
    // game memory or risking reads from uncommitted guest addresses.
    // The entry tracer must be provided by a guest-memory-aware host.
    if (!ret)
        throw std::logic_error("x86_trace entry requires guest memory-aware trace host");
    (void)ctx;
    return ret;
}

[[noreturn]] void x86_trap(X86Context&, std::uint32_t, const char* what) {
    throw std::runtime_error(what ? what : "Xbox guest trap");
}

void x86_resume(X86Context&, std::uint8_t*, const X86Resume&) {
    // The SDK uses special host-stack unwinding to reach a generated
    // catch continuation. Calling a target directly changes semantics.
    // Fail closed until the matching ARM64 exception-frame router exists.
    throw std::logic_error("x86_resume requires ARM64 guest exception continuation support");
}
