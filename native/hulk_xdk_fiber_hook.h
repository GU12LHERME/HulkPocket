#pragma once
#include "xbe_sdk_fiber_adapter.h"
#include "xbe_guest_fiber_bootstrap.h"
#include <cstdint>
#include <stdexcept>

namespace hulkpocket {

// Input addresses are taken from the *matching game's* upstream
// src/common/addresses.h, not guessed from the title ID.
// The entry point and XBE revision must be validated separately.
struct HulkXdkFiberLayout {
    std::uint32_t fiber_tls_index_va;
    std::uint32_t switch_to_fiber_va;
    std::uint32_t bootstrap_va;
};

// Upstream Hulk Recompiled, European and North-American Xbox binaries.
// These are guest VAs, not native Android pointers.
inline constexpr HulkXdkFiberLayout kHulkEuFiberLayout{
    0x004E7BDC, 0x00350EBD, 0x00350DD9
};
inline constexpr HulkXdkFiberLayout kHulkUsaFiberLayout{
    0x004E5A9C, 0x0035551D, 0x00355439
};

// XDK static TLS uses a DWORD index relative to StackBase, often negative
// (e.g. FFFFFFD3 = -45, for 180 bytes of TLS). The x86 scaled address
// wraps at 32 bits; reject addresses outside the supported per-thread
// TLS arena rather than treating every negative index as overflow.
inline uint32_t HulkFiberTlsSlot(uint32_t stack_base,uint32_t index){
    const int64_t signed_index=index<=0x7fffffffu?int64_t(index):int64_t(index)-0x100000000ll;
    const int64_t slot=int64_t(stack_base)+4*signed_index;
    if(!stack_base||stack_base%4||signed_index < -0x4000||signed_index > 0x4000||
       slot<=0||slot>0xfffffffcll)
        throw std::out_of_range("Invalid fiber TLS index");
    return uint32_t(slot);
}

// Extract the complete XDK 5849 fiber chain from the actual SDK context:
//
//   arg: guest[ESP+4] = next fiber record
//   stackBase = guest[FS_base+4]
//   tlsIndex  = guest[fiber_tls_index_va]
//   tls       = guest[stackBase + tlsIndex*4]
//   current   = guest[tls+0x10]
//
// After validation, save/restore the guest registers and perform
// a native AArch64 stack switch through the XbeSdkFiberAdapter.
// The root native stack needs explicit registration in the production
// XboxXBERecomp hook. This prototype expects Create() was already called.
template <typename Read32, typename Write32>
XboxGuestFiberSwap HulkXdkSwitchToFiber(
    XboxFiberScheduler& scheduler, X86Context& ctx,
    HulkXdkFiberLayout layout, Read32&& read32, Write32&& write32,
    XbeGuestFiberBootstrap* bootstrap = nullptr) {
    if (!layout.fiber_tls_index_va || !layout.switch_to_fiber_va ||
        !layout.bootstrap_va)
        throw std::invalid_argument("unconfigured Hulk region");
    if (ctx.esp > 0xFFFFFFFBu || ctx.fs_base > 0xFFFFFFFBu)
        throw std::out_of_range("guest address arithmetic overflow");
    const std::uint32_t next = read32(ctx.esp + 4);
    const std::uint32_t stack_base = read32(ctx.fs_base + 4);
    const std::uint32_t index = read32(layout.fiber_tls_index_va);
    if (!next) throw std::out_of_range("invalid Hulk guest TLS chain");
    const std::uint32_t tls = read32(HulkFiberTlsSlot(stack_base,index));
    if (!tls || tls > 0xFFFFFFEFu)
        throw std::out_of_range("invalid Hulk TLS pointer");
    const std::uint32_t current = read32(tls + 0x10);
    if (!current || scheduler.CurrentId() != current)
        throw std::logic_error("Hulk guest fiber is not the running native fiber");

    // A not-yet-started Xbox fiber must resume through the *correct
    // region's* XDK bootstrap. It cannot start at an arbitrary guest PC.
    const bool newly_created = !scheduler.Exists(next);
    if (newly_created) {
        const std::uint32_t saved = read32(next + 0x0C);
        if (saved > 0xFFFFFFEFu)
            throw std::out_of_range("invalid destination guest stack");
        const std::uint32_t initial_entry = read32(saved + 20);
        if (initial_entry != layout.bootstrap_va)
            throw std::logic_error("unsupported Xbox fiber continuation");
        // Real guest-code trampoline receives the verified XDK entry
        // and invokes the SDK's x86_icall on a fresh ARM64 native stack.
        if (!bootstrap)
            throw std::logic_error("new guest fiber requires native game trampoline");
        bootstrap->Register(next, initial_entry);
    }
    XbeSdkFiberAdapter bridge(scheduler, ctx);
    return bridge.SwitchTo(next, tls, read32, write32);
}
} // namespace hulkpocket
