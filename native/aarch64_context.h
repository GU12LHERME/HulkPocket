#pragma once
#include <cstdint>
#include <type_traits>

namespace hulkpocket {
// AAPCS64 preserved general-purpose and scalar FP registers.
// The pointed-to 16-byte-aligned stack is externally owned and must
// remain alive until all suspended contexts have completed.
struct alignas(16) Aarch64Context {
    uint64_t gpr[12]{}; // x19 through x30; gpr[11] is return address
    uint64_t sp{};
    uint64_t fp[8]{};  // low 64 bits of v8-v15, i.e. d8-d15
};
static_assert(sizeof(Aarch64Context) == 176);
static_assert(alignof(Aarch64Context) == 16);
static_assert(std::is_standard_layout_v<Aarch64Context>);
}

extern "C" void hulkpocket_aarch64_switch(
    hulkpocket::Aarch64Context* from,
    const hulkpocket::Aarch64Context* to);
