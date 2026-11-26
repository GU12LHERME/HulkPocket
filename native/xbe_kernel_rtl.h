#pragma once
#include "xbox_guest_address_space.h"
#include <string>
#include <algorithm>
#include <stdexcept>
namespace hulkpocket {
inline uint8_t GuestByte(const XboxGuestAddressSpace& mem,uint32_t addr) {
    uint8_t b=0; mem.Read(addr,&b,1); return b;
}
inline uint32_t GuestAnsiLength(const XboxGuestAddressSpace& mem,uint32_t addr) {
    if(!addr) return 0;
    for(uint32_t n=0;n<65535;++n) {
        if(uint64_t(addr)+n>=XboxGuestAddressSpace::kSize) break;
        if(!GuestByte(mem,addr+n)) return n;
    }
    throw std::runtime_error("Unterminated guest ANSI string");
}
inline void InitGuestAnsiString(XboxGuestAddressSpace& mem,uint32_t dest,uint32_t src) {
    if(!mem.IsCommitted(dest,8)) throw std::runtime_error("Invalid ANSI descriptor");
    const auto n=GuestAnsiLength(mem,src);
    mem.Write32(dest,n|((src?n+1:0)<<16));
    mem.Write32(dest+4,src);
}
inline int CompareGuestAnsiStrings(const XboxGuestAddressSpace& mem,uint32_t a,uint32_t b,bool fold) {
    auto lengths=[&](uint32_t addr) {
        if(!mem.IsCommitted(addr,8)) throw std::runtime_error("Invalid ANSI descriptor");
        const auto v=mem.Read32(addr);
        const auto n=v&65535u;
        if(n>(v>>16) || !mem.IsCommitted(mem.Read32(addr+4),n))
            throw std::runtime_error("Invalid ANSI buffer");
        return n;
    };
    auto n=lengths(a),m=lengths(b),ap=mem.Read32(a+4),bp=mem.Read32(b+4);
    auto upper=[fold](int c){return fold && c>='a' && c<='z'?c-32:c;};
    for(uint32_t i=0;i<std::min(n,m);++i) {
        int d=upper(GuestByte(mem,ap+i))-upper(GuestByte(mem,bp+i));
        if(d) return d;
    }
    return int(n)-int(m);
}
}
