#pragma once
#include "xbe_image_memory_loader.h"
#include "xbox_guest_address_space.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hulkpocket {

// XBE kernel thunk table as stored on disk: high-bit-tagged kernel
// ordinals followed by a zero terminator. This is a read-only report.
// The portable runtime does NOT implement these imported services.
struct XbeKernelImports {
    std::vector<std::uint32_t> ordinals;
    std::size_t entries = 0;
    bool complete = false;
    std::string error;
};

// Must be called after LoadXbeSections(). Fails closed on an invalid
// table, uncommitted page, an unrecognized value or missing terminator.
// Bound iteration to avoid reading unrelated game memory.
XbeKernelImports ScanXbeKernelImports(
    const LoadedXbeImage& image, const XboxGuestAddressSpace& memory,
    std::size_t max_entries = 512);

} // namespace hulkpocket
