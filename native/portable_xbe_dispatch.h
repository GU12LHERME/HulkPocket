#pragma once
#include "cpu/x86_context.h"
#include <cstddef>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <vector>

namespace hulkpocket {

// Android/Linux replacement for the *function mapping* portion of the
// XboxXBERecomp Windows dispatch.cpp. It dispatches functions already
// statically recompiled for the HOST ISA. It does NOT parse XBE code or
// translate x86 instructions and does not load PE/DLL images.
//
// The mappings must come from generated, legally sourced game code.
// No synthetic function is represented as actual Hulk game execution.
class PortableXbeDispatch {
public:
    // x86 SDK mapping: {guest address, native function pointer}.
    using KernelCall = void (*)(X86Context&, std::uint8_t*, std::uint32_t ordinal);
    PortableXbeDispatch(const X86FuncMapping* mappings, std::size_t count,
                        std::uint32_t code_lo, std::uint32_t code_hi,
                        KernelCall kernel = nullptr);
    PortableXbeDispatch(const PortableXbeDispatch&) = delete;
    PortableXbeDispatch& operator=(const PortableXbeDispatch&) = delete;

    // Must be called on the owning host thread. No global game dispatcher.
    void Call(X86Context& ctx, std::uint8_t* base, std::uint32_t target) const;
    bool Contains(std::uint32_t target) const;
    std::size_t FunctionCount() const noexcept { return mappings_.size(); }
private:
    std::vector<X86FuncMapping> mappings_;
    std::thread::id owner_;
    std::uint32_t lo_, hi_;
    KernelCall kernel_;
};

// A scoped, thread-local binding for the SDK's unmodified x86_icall ABI.
// It must outlive all guest fiber stacks using it. Never share between
// concurrently running Xbox guest kernel threads.
class ScopedXbeDispatch {
public:
    explicit ScopedXbeDispatch(const PortableXbeDispatch& dispatcher);
    ~ScopedXbeDispatch();
    ScopedXbeDispatch(const ScopedXbeDispatch&) = delete;
    ScopedXbeDispatch& operator=(const ScopedXbeDispatch&) = delete;
private:
    const PortableXbeDispatch* previous_;
};

// Mirrors the SDK declaration exactly. Implemented in the portable
// Android object, instead of Windows-only runtime/cpu/dispatch.cpp.
} // namespace hulkpocket
void x86_icall(X86Context& ctx, std::uint8_t* base, std::uint32_t target);
