#pragma once
#include "xbox_guest_address_space.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hulkpocket {

struct LoadedXbeImage {
    std::uint32_t base = 0;
    std::uint32_t image_size = 0;
    std::uint32_t entry = 0;
    std::uint32_t kernel_thunk = 0;
    std::uint32_t title_id = 0;
    std::size_t sections = 0;
    // XBE startup metadata needed by a future Xbox kernel bootstrap.
    // These fields are diagnostic only; no TLS/heap is initialized yet.
    std::uint32_t tls_va = 0;
    std::uint32_t stack_commit = 0;
    std::uint32_t heap_reserve = 0;
    std::uint32_t heap_commit = 0;
    std::uint32_t init_flags = 0;
};

// Load image headers and section bytes only, with strict bounds checking.
// This does NOT implement Xbox kernel startup, imports, TLS allocation,
// game resource loading, x86 instruction execution or security checks.
// The XBE data must come from the user's own lawfully obtained game.
LoadedXbeImage LoadXbeSections(
    const std::vector<std::uint8_t>& xbe_file, XboxGuestAddressSpace& memory);

} // namespace hulkpocket
