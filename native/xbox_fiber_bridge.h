#pragma once
#include "xbox_fiber_scheduler.h"
#include "xbox_guest_fiber_state.h"
#include <cstdint>
#include <stdexcept>

namespace hulkpocket {

// Couples the *guest x86* XDK 5849 fiber-frame protocol to the ARM64
// native-stack scheduler. Memory access is supplied by the embedding
// runtime; no host process pointers are treated as Xbox addresses.
// This is the host-side glue, not an XBE runtime or XboxXBE-Recomp hook.
class XboxFiberBridge {
public:
    XboxFiberBridge(XboxFiberScheduler& scheduler, XboxGuestFiberRegs& regs)
        : scheduler_(scheduler), regs_(regs) {}

    // Must be called from the current guest fiber. The caller resolves
    // the guest TLS pointer and provides checked guest-memory accessors.
    // The suspended C++ continuation resumes only when the target
    // switches back to it.
    template <typename Read32, typename Write32>
    XboxGuestFiberSwap SwitchTo(
        std::uint32_t next, std::uint32_t tls,
        Read32&& read32, Write32&& write32) {
        const std::uint32_t current = scheduler_.CurrentId();
        if (!current || !next || !tls ||
            !scheduler_.Exists(current) || !scheduler_.Exists(next) ||
            scheduler_.Finished(next))
            throw std::logic_error("invalid host or guest fiber transition");
        if (read32(tls + 0x10) != current)
            throw std::logic_error("guest TLS and native fiber disagree");

        // Matches the Windows port: even a switch to the same fiber
        // performs the x86 save/restore protocol before returning.
        const auto guest = SaveRestoreXboxFiber(
            regs_, current, next, tls, read32, write32);
        if (next != current) scheduler_.SwitchTo(next);
        return guest;
    }
private:
    XboxFiberScheduler& scheduler_;
    XboxGuestFiberRegs& regs_;
};

}  // namespace hulkpocket
