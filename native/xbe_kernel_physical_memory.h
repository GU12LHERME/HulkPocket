#pragma once
#include "xbox_guest_address_space.h"
#include <map>
#include <set>
#include <algorithm>
#include <stdexcept>
namespace hulkpocket {
// XboxXBERecomp memory.h reserves the first physical MiB for kernel data.
inline constexpr uint32_t kXboxPhysicalBase=0x80000000u;
inline constexpr uint32_t kXboxWriteCombineBase=0xF0000000u;
inline constexpr uint32_t kXboxKernelReservedBytes=0x00100000u;
inline void InitializeXboxKernelPhysicalRegion(XboxGuestAddressSpace& mem) {
    const auto page=mem.PageSize();
    for(uint64_t offset=0;offset<kXboxKernelReservedBytes;offset+=page)
        if(mem.IsCommitted(kXboxPhysicalBase+static_cast<uint32_t>(offset),1))
            throw std::runtime_error("Xbox kernel physical region overlaps guest mapping");
    // Anonymous zeroed RAM, like the upstream runtime before kernel data is initialized.
    // This is deliberately NOT a fabricated xboxkrnl PE image or executable firmware.
    mem.CommitSharedPair(kXboxPhysicalBase,kXboxWriteCombineBase,kXboxKernelReservedBytes);
}
// Physical and write-combine guest aliases share the same host RAM. No GPU MMIO.
class XbePhysicalMemory {
    XboxGuestAddressSpace& mem_;
    std::map<uint32_t,uint32_t> live_;
    std::set<uint32_t> persistent_;
    std::map<uint32_t,uint32_t> protection_;
    static constexpr uint32_t base=kXboxPhysicalBase, first=kXboxKernelReservedBytes, limit=0x04000000;
    static uint32_t Canonical(uint32_t a){
        return a>=kXboxWriteCombineBase&&a<kXboxWriteCombineBase+limit?
            base+(a-kXboxWriteCombineBase):a;
    }
public:
    static bool SupportedProtection(uint32_t p){return p==4||p==0x204||p==0x404;}
    explicit XbePhysicalMemory(XboxGuestAddressSpace& mem):mem_(mem) {}
    uint32_t Allocate(uint32_t bytes,uint32_t lowest,uint32_t highest,uint32_t alignment,uint32_t protection=4) {
        if(!SupportedProtection(protection))throw std::invalid_argument("Unsupported contiguous allocation protection");
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
                if(mem_.IsCommitted(base+static_cast<uint32_t>(p+off),1)||
                   mem_.IsCommitted(kXboxWriteCombineBase+static_cast<uint32_t>(p+off),1)) {
                    available=false;break;
                }
            }
            if(available) {
                uint32_t va=base+static_cast<uint32_t>(p);
                mem_.CommitSharedPair(va,kXboxWriteCombineBase+uint32_t(p),n);
                live_.emplace(va,static_cast<uint32_t>(n));protection_.emplace(va,protection);
                // Match the pinned recomp runtime's canonical allocation return.
                return va;
            }
            if(p<align) break;
            p-=align;
        }
        return 0;
    }
    bool Free(uint32_t address) {
        if(!address) return true;
        address=Canonical(address);
        auto it=live_.find(address);
        if(it==live_.end()) return false;
        for(uint64_t a=address;a<uint64_t(address)+it->second;a+=4096) persistent_.erase(static_cast<uint32_t>(a));
        mem_.Decommit(kXboxWriteCombineBase+(address-base),it->second);
        mem_.Decommit(address,it->second);protection_.erase(address);live_.erase(it);return true;
    }
    uint32_t Size(uint32_t address) const {
        address=Canonical(address);
        auto it=live_.upper_bound(address);
        if(it==live_.begin()) return 0;
        --it;
        uint64_t end=uint64_t(it->first)+it->second;
        return address<end?static_cast<uint32_t>(end-address):0;
    }
    bool Contains(uint32_t address,uint32_t bytes) const {
        if(!bytes)return true;
        address=Canonical(address);
        if(address<base||uint64_t(address)+bytes>uint64_t(base)+limit)return false;
        if(!mem_.IsCommitted(address,bytes))return false;
        // The reserved first MiB is kernel-owned, not an allocation.
        if(address<base+first)return uint64_t(address)+bytes<=uint64_t(base)+first;
        return Size(address)>=bytes;
    }
    bool Persist(uint32_t address,uint32_t bytes,bool enabled) {
        if(!Contains(address,bytes))return false;
        if(!bytes)return true;
        address=Canonical(address);
        // Xbox pages are 4 KiB regardless of Android host page size.
        for(uint64_t a=uint64_t(address)&~4095ull;a<uint64_t(address)+bytes;a+=4096){
            auto page=static_cast<uint32_t>(a);
            if(enabled)persistent_.insert(page);else persistent_.erase(page);
        }
        return true;
    }
    uint32_t AllocatedPages() const {
        uint64_t bytes=0;
        for(const auto& allocation:live_)bytes+=allocation.second;
        return uint32_t(bytes/4096);
    }
    bool IsPersistent(uint32_t address) const {return persistent_.count(Canonical(address)&~4095u)!=0;}
    uint32_t Protection(uint32_t address) const {
        if(!Contains(address,1))return 0;
        address=Canonical(address);
        auto it=protection_.upper_bound(address);
        return address<base+first||it==protection_.begin()?4:std::prev(it)->second;
    }
    uint32_t Physical(uint32_t address) const {
        if(!Contains(address,1)) return 0;
        return Canonical(address)-base;
    }
};
}
