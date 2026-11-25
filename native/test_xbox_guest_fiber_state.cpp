#include "xbox_guest_fiber_state.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

int main() {
    std::vector<std::uint32_t> m(65536, 0);
    auto read = [&](std::uint32_t at) -> std::uint32_t {
        if (at % 4 || at / 4 >= m.size()) throw std::out_of_range("guest read");
        return m[at / 4];
    };
    auto write = [&](std::uint32_t at, std::uint32_t v) {
        if (at % 4 || at / 4 >= m.size()) throw std::out_of_range("guest write");
        m[at / 4] = v;
    };
    constexpr std::uint32_t a = 0x1000, b = 0x1100, tls = 0x1200, fs = 0x1300;
    constexpr std::uint32_t stack_a = 0x8000, stack_b = 0x9000;
    constexpr std::uint32_t ret_a = 0x50EBD, ret_b = 0x50DD9;
    // As in the Windows hook, the target record holds a saved stack
    // frame: FS exception list, ebx, edi, esi, ebp, return address, arg.
    const std::array<std::uint32_t, 7> bframe = {
        0x5555, 0x1001, 0x1002, 0x1003, 0x1004, ret_b, 0xDEAD
    };
    for (std::size_t i = 0; i < bframe.size(); ++i) write(stack_b + i*4, bframe[i]);
    write(b + 0x0C, stack_b);
    write(fs, 0x7777);
    hulkpocket::XboxGuestFiberRegs ctx{};
    ctx.fs_base = fs;
    ctx.esp = stack_a;
    ctx.ebp = 0x2004; ctx.esi = 0x2003; ctx.edi = 0x2002; ctx.ebx = 0x2001;

    auto first = hulkpocket::SaveRestoreXboxFiber(ctx, a, b, tls, read, write);
    if (first.same_fiber || first.guest_entry != ret_b) return 1;
    if (read(a + 0x0C) != stack_a - 20 || read(tls + 0x10) != b ||
        read(stack_a - 20) != 0x7777 || read(stack_a - 16) != 0x2001 ||
        read(stack_a - 12) != 0x2002 || read(stack_a - 8) != 0x2003 ||
        read(stack_a - 4) != 0x2004) return 2;
    if (ctx.ebx != 0x1001 || ctx.edi != 0x1002 ||
        ctx.esi != 0x1003 || ctx.ebp != 0x1004 || read(fs) != 0x5555 ||
        ctx.esp != stack_b + 28 || ctx.eax != b || ctx.edx != tls || ctx.ecx != a)
        return 3;
    // Emulate a nested fiber switch back into A: its saved instruction
    // pointer/argument are provided by the original caller stack.
    write(stack_a, ret_a);
    write(stack_a + 4, 0xBEEF);
    ctx.ebx = 0x1001; ctx.edi = 0x1002; ctx.esi = 0x1003; ctx.ebp = 0x1004;
    ctx.esp = stack_b + 28;
    auto second = hulkpocket::SaveRestoreXboxFiber(ctx, b, a, tls, read, write);
    if (second.same_fiber || second.guest_entry != ret_a ||
        ctx.esp != stack_a + 8 || read(tls + 0x10) != a ||
        read(fs) != 0x7777 || ctx.ebx != 0x2001 || ctx.ebp != 0x2004)
        return 4;
    // Invalid guest pointers must be rejected, not treated as native addresses.
    bool rejected = false;
    try { hulkpocket::SaveRestoreXboxFiber(ctx, 0, b, tls, read, write); }
    catch (const std::invalid_argument&) { rejected = true; }
    if (!rejected) return 5;
    std::puts("XDK 5849 Xbox guest fiber save/restore: passed");
    return 0;
}
