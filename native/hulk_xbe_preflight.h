#pragma once
#include "xbe_image_memory_loader.h"
#include "portable_xbe_image.h"
#include <cstdint>
#include <string>

namespace hulkpocket {

// Validates that a privately loaded original Xbox image matches the
// recompiled ARM64 function map BEFORE any guest execution occurs.
// Does not certify full XBE revision or Xbox kernel compatibility.
struct XbePreflight {
    bool title_matches_hulk = false;
    bool entry_matches = false;
    bool entry_mapped = false;
    bool image_contains_entry = false;
    std::string region_candidate;
    std::string failure;
    bool ReadyForGuestCall() const noexcept {
        return title_matches_hulk && entry_matches && entry_mapped &&
               image_contains_entry;
    }
};

XbePreflight CheckHulkXbePreflight(
    const LoadedXbeImage& original, const PortableXbeImage& native_image);

} // namespace hulkpocket
