#pragma once
#include "xbe_startup_layout.h"
#include <algorithm>
namespace hulkpocket {
// Single-threaded bootstrap only. Caller owns the entire arena and must
// discard the guest address space if committing any range fails.
inline XbeStartupLayout InitializeXbeStartupMemory(
    const LoadedXbeImage& image, XboxGuestAddressSpace& memory,
    std::uint32_t arena_base, std::uint32_t arena_end) {
    const auto page = static_cast<std::uint32_t>(memory.PageSize());
    const auto layout = PlanXbeStartupLayout(image, arena_base, arena_end, page);
    for (std::uint64_t addr=arena_base; addr<arena_end; addr+=page)
        if (memory.IsCommitted(static_cast<std::uint32_t>(addr),page))
            throw std::invalid_argument("startup arena already occupied");
    memory.Commit(layout.stack_base,layout.stack_top-layout.stack_base);
    memory.Commit(layout.heap_base,layout.heap_committed);
    // Fresh anonymous pages are zero-filled. Leave guard and uncommitted
    // heap reservation inaccessible. This is not an Xbox heap allocator.
    return layout;
}
}
