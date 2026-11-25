#pragma once
#include "cpu/x86_context.h"
#include <cstdint>

// SDK-compatible declarations for the portable host runtime.
// The upstream x86_ops.h injects Windows DLL __declspec annotations;
// including that unmodified header in the host violates the Linux ABI.
// Recompiled guest code uses the generated ELF-rewritten x86_ops.h.
struct X86Resume {
    std::uint32_t target, esp;
};
void x86_icall(X86Context& ctx, std::uint8_t* base, std::uint32_t target);
void x86_resume(X86Context& ctx, std::uint8_t* base, const X86Resume& resume);
void x86_kernel(X86Context& ctx, std::uint8_t* base, std::uint32_t ordinal);
[[noreturn]] void x86_trap(X86Context& ctx, std::uint32_t va, const char* what);
void x86_priv(X86Context& ctx, std::uint32_t va, const char* what);
std::uint32_t x86_trace(X86Context& ctx, std::uint32_t va, std::uint32_t ret);
std::uint64_t x86_rdtsc();
void x86_cpuid(X86Context& ctx);
