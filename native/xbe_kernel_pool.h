#pragma once
#include "xbox_guest_address_space.h"
#include <map>
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
    uint32_t Allocate(uint32_t bytes) {
        if(!bytes || bytes>size_) return 0;
        uint64_t n=(uint64_t(bytes)+15)&~uint64_t(15);
        uint64_t pos=base_;
        for(const auto& item:live_) {
            if(pos+n<=item.first) break;
            pos=uint64_t(item.first)+item.second;
        }
        if(pos+n>uint64_t(base_)+size_) return 0;
        auto addr=static_cast<uint32_t>(pos);
        live_.emplace(addr,static_cast<uint32_t>(n));
        return addr;
    }
    void Free(uint32_t addr) {
        if(!addr) return;
        if(!live_.erase(addr)) throw std::invalid_argument("Invalid pool free");
    }
    size_t Outstanding() const { return live_.size(); }
};
}
