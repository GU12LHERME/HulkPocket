#pragma once
#include "xbox_guest_address_space.h"
#include <map>
#include <stdexcept>
#include <array>
namespace hulkpocket {
// Diagnostic private virtual arena; no physical aliases or executable pages.
class XbeVirtualMemory {
    XboxGuestAddressSpace& mem_;
    struct Region { uint32_t size, protect; };
    std::map<uint32_t,Region> regions_;
    static constexpr uint32_t low=0x10000000, high=0x30000000;
    static constexpr uint32_t invalid=0xc000000d, conflict=0xc0000018,
        no_memory=0xc0000017, missing=0xc000009f;
    auto Find(uint32_t base) {
        auto it=regions_.upper_bound(base);
        if(it==regions_.begin()) return regions_.end();
        --it;
        return uint64_t(base)<uint64_t(it->first)+it->second.size?it:regions_.end();
    }
    static uint64_t Round(uint64_t value,uint64_t align) {
        return (value+align-1)&~(align-1);
    }
    bool FreeRange(uint32_t base,uint32_t size) {
        if(base<low || uint64_t(base)+size>high) return false;
        auto it=regions_.lower_bound(base);
        if(it!=regions_.end() && it->first<uint64_t(base)+size) return false;
        if(it!=regions_.begin()) {
            auto prev=std::prev(it);
            if(uint64_t(prev->first)+prev->second.size>base) return false;
        }
        for(uint64_t p=base;p<uint64_t(base)+size;p+=mem_.PageSize())
            if(mem_.IsCommitted(static_cast<uint32_t>(p),1)) return false;
        return true;
    }
public:
    explicit XbeVirtualMemory(XboxGuestAddressSpace& mem):mem_(mem) {}
    uint32_t Allocate(uint32_t& base,uint32_t& size,uint32_t type,uint32_t protect) {
        // PAGE_READWRITE only. Reject unsupported flags instead of silently granting access.
        if(!size || protect!=4 || !(type&0x3000u) || (type&~0x3000u))
            return invalid;
        const uint64_t page=mem_.PageSize();
        const bool commit=type&0x1000u, reserve=(type&0x2000u)||(!base&&commit);
        if(reserve) {
            uint64_t start=base&~uint64_t(0xffff);
            uint64_t length=Round(uint64_t(base)+size,0x10000)-start;
            if(!length || length>high-low) return no_memory;
            if(!base) {
                start=low;
                for(const auto& kv:regions_) {
                    if(start+length<=kv.first) break;
                    start=Round(uint64_t(kv.first)+kv.second.size,0x10000);
                }
            }
            if(start+length>high || !FreeRange(static_cast<uint32_t>(start),static_cast<uint32_t>(length)))
                return base?conflict:no_memory;
            const auto a=static_cast<uint32_t>(start), n=static_cast<uint32_t>(length);
            if(commit) mem_.Commit(a,n);
            regions_.emplace(a,Region{n,protect});
            base=a;size=n;return 0;
        }
        auto it=Find(base);
        if(it==regions_.end()) return missing;
        const uint64_t first=uint64_t(base)&~(page-1), end=Round(uint64_t(base)+size,page);
        if(first<it->first || end>uint64_t(it->first)+it->second.size) return conflict;
        mem_.Commit(static_cast<uint32_t>(first),end-first);
        base=static_cast<uint32_t>(first);size=static_cast<uint32_t>(end-first);return 0;
    }
    uint32_t Free(uint32_t& base,uint32_t& size,uint32_t type) {
        if(type!=0x4000 && type!=0x8000) return invalid;
        auto it=Find(base);
        if(it==regions_.end()) return missing;
        if(type==0x8000) {
            if(base!=it->first || size) return invalid;
            mem_.Decommit(it->first,it->second.size);
            size=it->second.size;regions_.erase(it);return 0;
        }
        const uint64_t page=mem_.PageSize();
        const uint64_t first=uint64_t(base)&~(page-1);
        const uint64_t end=size?Round(uint64_t(base)+size,page):uint64_t(it->first)+it->second.size;
        if(first<it->first || end>uint64_t(it->first)+it->second.size || end<=first)
            return conflict;
        mem_.Decommit(static_cast<uint32_t>(first),end-first);
        base=static_cast<uint32_t>(first);size=static_cast<uint32_t>(end-first);return 0;
    }
    uint32_t Query(uint32_t addr,uint32_t out) {
        if(addr<low || addr>=high || !mem_.IsCommitted(out,28)) return invalid;
        const uint32_t page=static_cast<uint32_t>(mem_.PageSize()), base=addr&~(page-1);
        std::array<uint32_t,7> data{};
        data[0]=base;
        auto it=Find(addr);
        if(it==regions_.end()) {
            auto next=regions_.upper_bound(addr);
            data[3]=(next==regions_.end()?high:next->first)-base;
            data[4]=0x10000; data[5]=1;
        } else {
            bool committed=mem_.IsCommitted(base,1);
            uint32_t end=base+page;
            while(end<it->first+it->second.size && mem_.IsCommitted(end,1)==committed) end+=page;
            data[1]=it->first;data[2]=it->second.protect;data[3]=end-base;
            data[4]=committed?0x1000:0x2000;data[5]=committed?4:0;data[6]=0x20000;
        }
        mem_.Write(out,data.data(),sizeof(data));return 0;
    }
    uint32_t ReservedBytes() const {
        uint64_t bytes=0;
        for(const auto& r:regions_)bytes+=r.second.size;
        return uint32_t(bytes);
    }
    uint32_t CommittedBytes() const {
        uint64_t bytes=0;
        for(const auto& r:regions_)
            for(uint64_t p=r.first;p<uint64_t(r.first)+r.second.size;p+=mem_.PageSize())
                if(mem_.IsCommitted(uint32_t(p),1))bytes+=mem_.PageSize();
        return uint32_t(bytes);
    }
    uint32_t AllocationSize(uint32_t addr) {
        auto it=Find(addr);
        return it==regions_.end()?0:it->second.size-(addr-it->first);
    }
};
}
