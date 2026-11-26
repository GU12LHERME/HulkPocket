#pragma once
#include "hulk_kernel_export_kinds.h"
#include <array>
#include <stdexcept>
namespace hulkpocket {
class HulkKernelDataRegistry {
    struct Slot { uint32_t address=0; size_t bytes=0; };
    std::array<Slot,400> slots_{};
public:
    void Register(uint32_t ordinal, uint32_t address, size_t bytes,
                  const XboxGuestAddressSpace& memory) {
        if (HulkKernelExportKind(ordinal)!=KernelExportKind::Data ||
            !address || bytes<4 || !memory.IsCommitted(address,bytes))
            throw std::invalid_argument("Invalid kernel data registration");
        if(slots_[ordinal].address)
            throw std::invalid_argument("Duplicate kernel data registration");
        for(const auto& slot:slots_) {
            if(slot.address && uint64_t(address)<uint64_t(slot.address)+slot.bytes &&
                uint64_t(slot.address)<uint64_t(address)+bytes)
                throw std::invalid_argument("Overlapping kernel data registrations");
        }
        slots_[ordinal]={address,bytes};
    }
    KernelExportBinding Resolve(uint32_t ordinal) const {
        const auto kind=HulkKernelExportKind(ordinal);
        if(kind==KernelExportKind::Function) return {kind,0};
        if(kind==KernelExportKind::Data && slots_[ordinal].address)
            return {kind,slots_[ordinal].address};
        return {};
    }
    std::vector<uint32_t> MissingData(const XbeKernelImports& imports) const {
        if(!imports.complete) throw std::invalid_argument("Incomplete import scan");
        std::vector<uint32_t> missing;
        for(auto ordinal:imports.ordinals)
            if(HulkKernelExportKind(ordinal)==KernelExportKind::Data &&
               !slots_[ordinal].address) missing.push_back(ordinal);
        return missing;
    }
};
}
