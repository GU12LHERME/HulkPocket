// Diagnostic only: unsupported guest operations stop execution explicitly.
#include "portable_xbe_dispatch.h"
#include "xbe_guest_cpu_primitives.h"
#include <stdexcept>
#include <string>
#include <cstdint>
struct X86Resume;
[[noreturn]] static void stop(const char* operation, uint32_t address) {
    throw std::runtime_error(std::string(operation) + " at " + std::to_string(address));
}
void x86_trap(X86Context&, uint32_t address, const char* reason) {
    stop(reason ? reason : "guest trap", address);
}
void x86_priv(X86Context&, uint32_t address, const char* reason) {
    stop(reason ? reason : "privileged guest instruction", address);
}
void x86_kernel(X86Context& ctx, uint8_t* base, uint32_t ordinal) {
    if (ordinal == 0 || ordinal >= 400) stop("invalid kernel ordinal", ordinal);
    x86_icall(ctx, base, 0xFE000000u + ordinal * 4u);
}
void x86_resume(X86Context&, uint8_t*, const X86Resume&) {
    stop("guest continuation not implemented", 0);
}
void x86_cpuid(X86Context& ctx) { hulkpocket::GuestCpuId(ctx); }
uint64_t x86_rdtsc() { return hulkpocket::GuestTimestamp(); }
// MIT Hulk Windows reference, pinned 6603e8a4021a06331f63310ce47b42315d9dbfc6,
// src/game/hooks.cpp; USA config hook 0x3B995 -> 0x3BA1D.
// This is a recompiler conditional hook, not a guest function return.
// The generated branch skips optional owner access only when EAX is null.
bool HasNoActorUpdateOwner(X86Context& ctx, uint8_t*) {
    return ctx.eax == 0;
}
