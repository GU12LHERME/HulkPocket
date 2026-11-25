#include "xbox_guest_address_space.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

using hulkpocket::XboxGuestAddressSpace;
int main() {
    XboxGuestAddressSpace memory;
    if (!memory.Base() || XboxGuestAddressSpace::kSize != 0x100000000ULL)
        return 1;
    const auto page = memory.PageSize();
    if (!page || page * 4 >= 0x100000000ULL) return 2;
    const std::uint32_t start = static_cast<std::uint32_t>(page * 2);
    if (memory.IsCommitted(start, 4)) return 3;

    bool rejected = false;
    try { memory.Read32(start); }
    catch (const std::out_of_range&) { rejected = true; }
    if (!rejected) return 4;
    rejected = false;
    try { memory.Commit(0, 4); }
    catch (const std::out_of_range&) { rejected = true; }
    if (!rejected) return 5;

    memory.Commit(start, page + 16);
    memory.Write32(start, 0x12345678);
    if (memory.Read32(start) != 0x12345678 ||
        !memory.IsCommitted(start, page + 16)) return 6;

    // Cross-page, unaligned read: guest x86 allows unaligned DWORDs.
    const auto edge = start + static_cast<std::uint32_t>(page) - 2;
    memory.Write32(edge, 0xDEADBEEF);
    if (memory.Read32(edge) != 0xDEADBEEF ||
        memory.Base()[edge] != 0xEF) return 7;

    rejected = false;
    try { memory.Read32(start + static_cast<std::uint32_t>(3 * page)); }
    catch (const std::out_of_range&) { rejected = true; }
    if (!rejected) return 8;

    // Highest valid 32-bit guest page must be addressable from base
    // while four bytes crossing 4 GiB must fail, without pointer wrap.
    memory.Commit(0xFFFFF000u, 4096);
    memory.Write32(0xFFFFFFFCu, 0xCAFEBABE);
    if (memory.Read32(0xFFFFFFFCu) != 0xCAFEBABE) return 9;
    rejected = false;
    try { memory.Read32(0xFFFFFFFFu); }
    catch (const std::out_of_range&) { rejected = true; }
    if (!rejected) return 10;

    // Guest memory is actually usable directly by generated X86_MEM*
    // macros as a 64-bit host pointer + 32-bit Xbox virtual address.
    memory.Base()[start] = 0x33;
    if ((memory.Read32(start) & 0xFFu) != 0x33u) return 11;
    std::puts("Sparse Xbox 4 GiB virtual address space: checked ARM64 guest memory passed");
    return 0;
}
