#pragma once
#include "hulk_kernel_data_registry.h"
namespace hulkpocket {
// Uncommitted pages deliberately fault on access. Never use for normal boot.
struct MissingKernelDataGuard {
    uint32_t ordinal;
    uint32_t address;
};
inline std::vector<MissingKernelDataGuard> PlanMissingKernelDataGuards(
    const HulkKernelDataRegistry& registry,const XbeKernelImports& imports,
    const XboxGuestAddressSpace& memory,uint32_t base,uint32_t end) {
    const auto page=memory.PageSize();
    if(!base || base%page || end%page || end<=base)
        throw std::invalid_argument("Invalid kernel guard arena");
    std::vector<MissingKernelDataGuard> result;
    uint64_t cursor=base;
    for(auto ordinal:registry.MissingData(imports)) {
        if(cursor+page>end || memory.IsCommitted(static_cast<uint32_t>(cursor),page))
            throw std::runtime_error("Kernel guard arena unavailable");
        result.push_back({ordinal,static_cast<uint32_t>(cursor)});
        cursor+=page;
    }
    return result;
}
}
