#pragma once
#include "xbe_image_memory_loader.h"
#include <cstdint>
#include <stdexcept>

namespace hulkpocket {
// Experimental layout only: caller must reserve these ranges in its
// guest allocator before use. No Xbox heap objects or TLS are created.
struct XbeStartupLayout {
    std::uint32_t guard, stack_base, stack_top, heap_base;
    std::uint32_t heap_reserved, heap_committed;
};
inline XbeStartupLayout PlanXbeStartupLayout(
    const LoadedXbeImage& image, std::uint32_t arena_base,
    std::uint32_t arena_end, std::uint32_t page) {
    using U = std::uint64_t;
    if (!page || (page & (page-1)) || page < 4096 ||
        arena_base < page || arena_base % page || arena_end % page ||
        arena_end <= arena_base)
        throw std::invalid_argument("invalid startup arena");
    if (!image.stack_commit || !image.heap_reserve ||
        image.heap_commit > image.heap_reserve)
        throw std::invalid_argument("invalid XBE startup sizes");
    auto align = [page](U n) { return (n + page-1) & ~U(page-1); };
    const U image_end = U(image.base) + image.image_size;
    if (image_end > (U(1)<<32) ||
        (U(arena_base) < align(image_end) &&
         U(arena_end) > (U(image.base) & ~U(page-1))))
        throw std::invalid_argument("startup arena overlaps image");
    const U stack_base = U(arena_base) + page;
    const U stack_top = stack_base + align(image.stack_commit);
    const U heap_reserved = align(image.heap_reserve);
    const U heap_committed = align(image.heap_commit);
    if (stack_top + heap_reserved > arena_end)
        throw std::invalid_argument("startup arena too small");
    return {arena_base, static_cast<std::uint32_t>(stack_base),
        static_cast<std::uint32_t>(stack_top),
        static_cast<std::uint32_t>(stack_top),
        static_cast<std::uint32_t>(heap_reserved),
        static_cast<std::uint32_t>(heap_committed)};
}
} // namespace hulkpocket
