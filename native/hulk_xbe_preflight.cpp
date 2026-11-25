#include "hulk_xbe_preflight.h"
#include <stdexcept>

namespace hulkpocket {
XbePreflight CheckHulkXbePreflight(
    const LoadedXbeImage& original, const PortableXbeImage& native_image) {
    XbePreflight result;
    constexpr std::uint32_t hulk_title = 0x56550039u;
    result.title_matches_hulk = original.title_id == hulk_title;
    result.entry_matches = original.entry == native_image.Entry();
    const std::uint64_t end = std::uint64_t(original.base) + original.image_size;
    result.image_contains_entry =
        original.entry >= original.base && std::uint64_t(original.entry) < end;
    // Use SDK function map rather than only the image address bounds:
    // the recompiler may not have emitted the original entry function.
    const auto dispatcher = native_image.CreateDispatcher();
    result.entry_mapped = dispatcher->Contains(original.entry);
    if (original.entry == 0x0020AAC6u) result.region_candidate = "EU";
    else if (original.entry == 0x0020A016u) result.region_candidate = "USA";
    else result.region_candidate = "unknown";
    if (!result.title_matches_hulk)
        result.failure = "XBE title ID is not Hulk Ultimate Destruction";
    else if (!result.image_contains_entry)
        result.failure = "XBE entry outside guest image range";
    else if (!result.entry_matches)
        result.failure = "XBE and ARM64 recompiled module entries differ";
    else if (!result.entry_mapped)
        result.failure = "original XBE entry absent from ARM64 recompiler mapping";
    return result;
}
}
