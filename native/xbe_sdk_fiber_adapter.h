#pragma once
#include "xbox_fiber_scheduler.h"
#include "xbox_guest_fiber_state.h"
#include "cpu/x86_context.h"
#include <cstdint>
#include <stdexcept>

namespace hulkpocket {

// Experimental adapter to the REAL XboxXBERecomp SDK X86Context.
// Memory callbacks must access checked guest memory. The embedding
// game hook supplies the guest TLS pointer; no Win32 calls are needed.
// Unlike a dummy standalone simulation, this updates the actual SDK
// X86Context before suspending the native C++ continuation.
class XbeSdkFiberAdapter {
public:
    XbeSdkFiberAdapter(XboxFiberScheduler& scheduler, X86Context& ctx)
        : scheduler_(scheduler), ctx_(ctx) {}

    template <typename Read32, typename Write32>
    XboxGuestFiberSwap SwitchTo(
        std::uint32_t next, std::uint32_t tls,
        Read32&& read32, Write32&& write32) {
        const std::uint32_t current = scheduler_.CurrentId();
        if (!current || !next || !tls ||
            !scheduler_.Exists(current) || !scheduler_.Exists(next) ||
            scheduler_.Finished(next))
            throw std::logic_error("invalid guest-native fiber switch");
        if (read32(tls + 0x10) != current)
            throw std::logic_error("XBE guest TLS and native fiber disagree");

        // X86Context belongs to the guest thread. The SDK passes the same
        // object through the hook in both native fiber continuations.
        XboxGuestFiberRegs state{
            ctx_.eax, ctx_.ecx, ctx_.edx, ctx_.ebx, ctx_.esp,
            ctx_.ebp, ctx_.esi, ctx_.edi, ctx_.fs_base
        };
        const auto swap = SaveRestoreXboxFiber(
            state, current, next, tls, read32, write32);

        // Commit the DESTINATION guest state before handing execution to
        // its native C++ stack; other x86 state stays in the SDK object.
        ctx_.eax = state.eax;
        ctx_.ecx = state.ecx;
        ctx_.edx = state.edx;
        ctx_.ebx = state.ebx;
        ctx_.esp = state.esp;
        ctx_.ebp = state.ebp;
        ctx_.esi = state.esi;
        ctx_.edi = state.edi;
        ctx_.fs_base = state.fs_base;

        // CRITICAL: after switching back here, the shared X86Context
        // has ALREADY been restored by the other fiber's SwitchTo().
        // Do not copy stale per-call 'state' back on resume.
        if (next != current) scheduler_.SwitchTo(next);
        return swap;
    }
private:
    XboxFiberScheduler& scheduler_;
    X86Context& ctx_;
};
}
