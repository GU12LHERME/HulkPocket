#pragma once
#include <cstdint>
#include <stdexcept>

namespace hulkpocket {

// Guest x86 state involved in Thunderclap/XboxXBE-Recomp's XDK 5849
// SwitchToFiber hook. Addresses refer to the game's 32-bit guest memory,
// never to ARM64 host pointers.
struct XboxGuestFiberRegs {
    std::uint32_t eax = 0, ecx = 0, edx = 0, ebx = 0;
    std::uint32_t esp = 0, ebp = 0, esi = 0, edi = 0;
    std::uint32_t fs_base = 0;
};

struct XboxGuestFiberSwap {
    std::uint32_t guest_entry = 0;
    bool same_fiber = false;
};

// `read32(addr)` and `write32(addr,value)` must implement checked guest
// memory accesses (e.g. the SDK's R32/W32). The caller resolves the
// current/next fiber pointers and guest TLS from the game itself.
// Applies the exact x86 register / SEH / guest stack protocol described
// in the playable Windows port's src/game/fibers.cpp.
// This does NOT switch ARM64 stacks, call game code, or resolve guest TLS.
template <typename Read32, typename Write32>
XboxGuestFiberSwap SaveRestoreXboxFiber(
    XboxGuestFiberRegs& ctx, std::uint32_t current,
    std::uint32_t next, std::uint32_t tls,
    Read32&& read32, Write32&& write32) {
    if (!current || !next || !tls) throw std::invalid_argument("invalid Xbox fiber record");
    if (ctx.esp < 20) throw std::out_of_range("invalid Xbox guest stack");

    // Original Xbox XDK 5849 stack protocol: push ebp, esi, edi, ebx,
    // then exception chain pointer fs:[0]. All writes are guest memory.
    ctx.eax = next;
    ctx.edx = tls;
    ctx.ecx = current;
    auto push = [&](std::uint32_t v) {
        ctx.esp -= 4;
        write32(ctx.esp, v);
    };
    push(ctx.ebp);
    push(ctx.esi);
    push(ctx.edi);
    push(ctx.ebx);
    push(read32(ctx.fs_base));
    write32(current + 0x0C, ctx.esp);

    ctx.esp = read32(next + 0x0C);
    write32(tls + 0x10, next);
    write32(ctx.fs_base, read32(ctx.esp)); ctx.esp += 4;
    ctx.ebx = read32(ctx.esp); ctx.esp += 4;
    ctx.edi = read32(ctx.esp); ctx.esp += 4;
    ctx.esi = read32(ctx.esp); ctx.esp += 4;
    ctx.ebp = read32(ctx.esp); ctx.esp += 4;
    const auto entry = read32(ctx.esp);
    ctx.esp += 8;  // guest return address and stdcall arg
    return {entry, next == current};
}
}
