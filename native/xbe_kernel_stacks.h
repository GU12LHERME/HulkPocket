#pragma once
#include "xbox_guest_address_space.h"
#include <map>
#include <stdexcept>
#include <cstdint>
namespace hulkpocket {
// Separate guest stack arena, after the 16 thread slots and below
// the unsupported kernel data guard range. No host stack switching here.
// Match pinned XboxXBERecomp's 64 KiB stack rounding and guard area.
class XbeKernelStacks {
    XboxGuestAddressSpace& mem_;
    struct Stack {uint32_t guard,limit,top,requested;};
    std::map<uint32_t,Stack> live_;
    static constexpr uint32_t low=0x0e000000u,high=0x0f000000u,gran=0x10000u;
public:
    explicit XbeKernelStacks(XboxGuestAddressSpace& mem):mem_(mem){}
    uint32_t Allocate(uint32_t bytes){
        uint64_t size=(uint64_t(bytes)+gran-1)&~uint64_t(gran-1);
        if(!bytes||size+gran>high-low)return 0;
        for(uint64_t a=low;a+gran+size<=high;a+=gran){
            bool free=true;
            for(const auto& item:live_){
                auto& st=item.second;
                if(a<st.top&&a+gran+size>st.guard){free=false;break;}
            }
            if(!free)continue;
            for(uint64_t p=a;p<a+gran+size;p+=mem_.PageSize())
                if(mem_.IsCommitted(uint32_t(p),1)){free=false;break;}
            if(!free)continue;
            auto limit=uint32_t(a+gran),top=uint32_t(a+gran+size);
            mem_.Commit(limit,size);
            live_.emplace(top,Stack{uint32_t(a),limit,top,bytes});
            return top;
        }
        return 0;
    }
    bool Free(uint32_t top,uint32_t limit,uint32_t active_esp){
        auto it=live_.find(top);
        if(it==live_.end())return false;
        const auto st=it->second;
        // Xbox callers may keep the requested rather than rounded limit.
        if(limit!=st.limit&&limit!=top-st.requested)return false;
        if(active_esp>=st.limit&&active_esp<=st.top)return false;
        mem_.Decommit(st.limit,st.top-st.limit);
        live_.erase(it);return true;
    }
    uint32_t Pages() const {
        uint32_t pages=0;
        for(const auto& item:live_)pages+=(item.second.top-item.second.limit)/4096;
        return pages;
    }
};
}
