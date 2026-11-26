#include "portable_xbe_cpu_services.h"
#include "portable_xbe_dispatch.h"
#include "xbe_guest_cpu_primitives.h"
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

void x86_cpuid(X86Context& ctx) { hulkpocket::GuestCpuId(ctx); }
std::uint64_t x86_rdtsc() { return hulkpocket::GuestTimestamp(); }

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
