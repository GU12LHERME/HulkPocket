/* Standalone Linux/AArch64 runtime test for the same switch assembly used by
 * the Android NDK cross-link. Does not execute any copyrighted game code.
 */
#include "aarch64_context.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

static hulkpocket::Aarch64Context main_ctx, guest_ctx;
alignas(16) static std::array<std::uint8_t, 65536> guest_stack{};
static volatile int stage = 0;

[[noreturn]] static void guest_entry() {
    stage = 1;
    hulkpocket_aarch64_switch(&guest_ctx, &main_ctx);
    stage = 3;  // Must resume on the same native C++ stack.
    hulkpocket_aarch64_switch(&guest_ctx, &main_ctx);
    std::abort(); // No safe returning path for a fresh context.
}

int main() {
    const auto top = reinterpret_cast<std::uintptr_t>(
        guest_stack.data() + guest_stack.size()) & ~std::uintptr_t(15);
    guest_ctx.sp = top;
    guest_ctx.gpr[11] = reinterpret_cast<std::uintptr_t>(&guest_entry);
    hulkpocket_aarch64_switch(&main_ctx, &guest_ctx);
    if (stage != 1) return 1;
    stage = 2;
    hulkpocket_aarch64_switch(&main_ctx, &guest_ctx);
    if (stage != 3) return 2;
    std::puts("AArch64 fiber suspension and resume: passed");
    return 0;
}
