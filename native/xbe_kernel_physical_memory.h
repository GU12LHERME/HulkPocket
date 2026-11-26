#pragma once
#include "xbox_guest_address_space.h"
#include <map>
#include <algorithm>
#include <stdexcept>
namespace hulkpocket {
// XboxXBERecomp memory.h reserves the first physical MiB for kernel data.
inline constexpr uint32_t kXboxPhysicalBase=0x80000000u;
inline constexpr uint32_t kXboxKernelReservedBytes=0x00100000u;
inline void InitializeXboxKernelPhysicalRegion(XboxGuestAddressSpace& mem) {
    const auto page=mem.PageSize();
    for(uint64_t offset=0;offset<kXboxKernelReservedBytes;offset+=page)
        if(mem.IsCommitted(kXboxPhysicalBase+static_cast<uint32_t>(offset),1))
            throw std::runtime_error("Xbox kernel physical region overlaps guest mapping");
    // Anonymous zeroed RAM, like the upstream runtime before kernel data is initialized.
    // This is deliberately NOT a fabricated xboxkrnl PE image or executable firmware.
    mem.Commit(kXboxPhysicalBase,kXboxKernelReservedBytes);
}
// Original Xbox physical-address convention; no GPU/MMIO or write-combine alias.
class XbePhysicalMemory {
    XboxGuestAddressSpace& mem_;
    std::map<uint32_t,uint32_t> live_;
    static constexpr uint32_t base=kXboxPhysicalBase, first=kXboxKernelReservedBytes, limit=0x04000000;
public:
    explicit XbePhysicalMemory(XboxGuestAddressSpace& mem):mem_(mem) {}
    uint32_t Allocate(uint32_t bytes,uint32_t lowest,uint32_t highest,uint32_t alignment) {
        const uint64_t page=mem_.PageSize();
        const uint64_t align=std::max<uint64_t>(alignment,page);
        if(!bytes || (align&(align-1)) || align>limit || lowest>=limit || lowest>highest)
            return 0;
        const uint64_t n=(uint64_t(bytes)+page-1)&~(page-1);
        const uint64_t lo=std::max(lowest,first), hi=std::min<uint64_t>(uint64_t(highest)+1,limit);
        if(n>hi || hi-n<lo) return 0;
        // Highest fit preserves the low address range for devices that need it.
        uint64_t p=(hi-n)&~(align-1);
        while(p>=lo) {
            bool available=true;
            for(uint64_t off=0;off<n;off+=page) {
                if(mem_.IsCommitted(base+static_cast<uint32_t>(p+off),1)) {
                    available=false;break;
                }
            }
            if(available) {
                uint32_t va=base+static_cast<uint32_t>(p);
                mem_.Commit(va,n);live_.emplace(va,static_cast<uint32_t>(n));return va;
            }
            if(p<align) break;
            p-=align;
        }
        return 0;
    }
    bool Free(uint32_t address) {
        if(!address) return true;
        auto it=live_.find(address);
        if(it==live_.end()) return false;
        mem_.Decommit(address,it->second);live_.erase(it);return true;
    }
    uint32_t Size(uint32_t address) const {
        auto it=live_.upper_bound(address);
        if(it==live_.begin()) return 0;
        --it;
        uint64_t end=uint64_t(it->first)+it->second;
        return address<end?static_cast<uint32_t>(end-address):0;
    }
    uint32_t Physical(uint32_t address) const {
        if(!Size(address)) return 0;
        return address-base;
    }
};
}
