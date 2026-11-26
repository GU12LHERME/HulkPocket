// Diagnostic only: unsupported guest operations stop execution explicitly.
#include "portable_xbe_dispatch.h"
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
void x86_cpuid(X86Context&) { stop("guest CPUID not implemented", 0); }
uint64_t x86_rdtsc() { stop("guest timestamp not implemented", 0); }
void HasNoActorUpdateOwner(X86Context&, uint8_t*) {
    stop("Hulk actor owner hook not implemented", 0);
}
