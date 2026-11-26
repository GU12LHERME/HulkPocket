#pragma once
#include "xbe_image_memory_loader.h"
#include <array>
#include <cstdio>
#include <map>
#include <stdexcept>

namespace hulkpocket {
// The diagnostic loader eagerly maps every XBE section (including
// non-preloaded resources). XeLoadSection / XeUnloadSection implement
// logical residency and guest-visible references, not page eviction.
// Never trust a guest-provided section address or mutable geometry.
class XbeResidentSections {
    XboxGuestAddressSpace& mem_;
    struct Section { std::array<uint32_t,6> immutable{}; };
    std::map<uint32_t,Section> sections_;
    static constexpr uint32_t invalid=0xc000000du;
    static uint32_t Read(const std::vector<uint8_t>& data,uint64_t off){
        if(off>data.size()||data.size()-off<4)throw std::invalid_argument("Invalid original section table");
        return uint32_t(data[off])|(uint32_t(data[off+1])<<8)|
               (uint32_t(data[off+2])<<16)|(uint32_t(data[off+3])<<24);
    }
public:
    XbeResidentSections(XboxGuestAddressSpace& mem,const LoadedXbeImage& image,
                        const std::vector<uint8_t>& original):mem_(mem){
        const auto table=Read(original,0x120),count=Read(original,0x11c);
        const auto header_size=Read(original,0x108);
        if(table<image.base||count!=image.sections||count>512||
           uint64_t(table)-image.base+56ull*count>header_size||
           uint64_t(table)+56ull*count>XboxGuestAddressSpace::kSize)
            throw std::invalid_argument("Invalid resident XBE table");
        for(uint32_t i=0;i<count;++i){
            auto addr=table+56*i;
            Section s{};
            for(unsigned j=0;j<6;++j)s.immutable[j]=Read(original,uint64_t(addr)-image.base+4*j);
            const auto va=s.immutable[1],size=s.immutable[2];
            if(!mem_.IsCommitted(addr,56)||!size||!mem_.IsCommitted(va,size))
                throw std::invalid_argument("Original section not resident");
            for(unsigned j=0;j<6;++j)
                if(mem_.Read32(addr+4*j)!=s.immutable[j])
                    throw std::invalid_argument("Original section header changed");
            sections_.emplace(addr,s);
        }
        // XBE preloaded sections start with one reference as on Xbox.
        for(const auto& [addr,s]:sections_)
            mem_.Write32(addr+0x18,(s.immutable[0]&2u)?1u:0u);
    }
    uint32_t Change(uint32_t addr,bool load){
        auto it=sections_.find(addr);
        if(it==sections_.end()||!mem_.IsCommitted(addr,56))return invalid;
        const auto& s=it->second.immutable;
        for(unsigned j=0;j<6;++j)
            if(mem_.Read32(addr+4*j)!=s[j])return invalid;
        if(!mem_.IsCommitted(s[1],s[2]))return invalid;
        auto refs=mem_.Read32(addr+0x18);
        if((load&&refs==0xffffffffu)||(!load&&!refs))return invalid;
        auto next=load?refs+1:refs-1;
        mem_.Write32(addr+0x18,next);
        std::fprintf(stderr,"XBOX SECTION %s PASS header=%08X va=%08X bytes=%u flags=%08X refs=%u->%u (eager resident, no eviction)\n",
            load?"LOAD":"UNLOAD",addr,s[1],s[2],s[0],refs,next);
        return 0;
    }
};
}
