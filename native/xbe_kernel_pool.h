#pragma once
#include "xbox_guest_address_space.h"
#include <map>
#include <cstring>
#include <algorithm>
#include <stdexcept>
namespace hulkpocket {
class XbeKernelPool {
    XboxGuestAddressSpace& mem_;
    uint32_t base_,size_;
    std::map<uint32_t,uint32_t> live_;
public:
    XbeKernelPool(XboxGuestAddressSpace& mem,uint32_t base,uint32_t size)
        :mem_(mem),base_(base),size_(size) {
        if(!base || base%mem.PageSize() || !size || size%mem.PageSize() ||
           uint64_t(base)+size>XboxGuestAddressSpace::kSize ||
           mem.IsCommitted(base,size))
            throw std::invalid_argument("Invalid pool arena");
        for(uint64_t a=base;a<uint64_t(base)+size;a+=mem.PageSize())
            if(mem.IsCommitted(static_cast<uint32_t>(a),1))
                throw std::invalid_argument("Pool overlaps committed memory");
        mem.Commit(base,size);
    }
    uint32_t Allocate(uint32_t bytes,uint32_t alignment=16) {
        if(!bytes || bytes>size_ || alignment<16 || (alignment&(alignment-1))) return 0;
        uint64_t n=(uint64_t(bytes)+15)&~uint64_t(15);
        auto align=[&](uint64_t a){return (a+alignment-1)&~uint64_t(alignment-1);};
        uint64_t pos=align(base_);
        for(const auto& item:live_) {
            if(pos+n<=item.first) break;
            pos=std::max(pos,align(uint64_t(item.first)+item.second));
        }
        if(pos+n>uint64_t(base_)+size_) return 0;
        auto addr=static_cast<uint32_t>(pos);
        live_.emplace(addr,static_cast<uint32_t>(n));
        std::memset(mem_.Base()+addr,0,static_cast<size_t>(n));
        return addr;
    }
    void Free(uint32_t addr) {
        if(!addr) return;
        if(!live_.erase(addr)) throw std::invalid_argument("Invalid pool free");
    }
    uint32_t AllocationSize(uint32_t addr) const {
        auto it=live_.upper_bound(addr);
        if(it==live_.begin())return 0;
        --it;
        auto end=uint64_t(it->first)+it->second;
        return addr<end?static_cast<uint32_t>(end-addr):0;
    }
    size_t Outstanding() const { return live_.size(); }
};
}
