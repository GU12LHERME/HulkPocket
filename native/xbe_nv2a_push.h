#pragma once
#include "xbox_guest_address_space.h"
#include <vector>
#include <stdexcept>
namespace hulkpocket {
struct Nv2aMethod {uint32_t method,value;};
// Decode the XDK scratch buffer method packets, as in upstream Walk.
// Reject control-flow packets, nonzero subchannels and truncated input.
// No GPU commands are executed by this parser; callers must explicitly
// implement the meaning of each method before resuming guest rendering.
inline std::vector<Nv2aMethod> DecodeNv2aMethods(XboxGuestAddressSpace& mem,uint32_t begin,uint32_t end){
    if(begin%4||end%4||end<begin||end-begin>0x100000||!mem.IsCommitted(begin,end-begin))
        throw std::invalid_argument("Invalid NV2A scratch buffer bounds");
    std::vector<Nv2aMethod> result;
    for(uint64_t p=begin;p<end;){
        uint32_t head=mem.Read32(uint32_t(p));p+=4;
        auto method=head&0x1ffcu,n=(head>>18)&0x7ffu;
        bool repeated=(head&0x40000000u)!=0;
        if((head&0xa003e003u)||uint64_t(n)*4>end-p||
            (!repeated&&n&&method+4*(n-1)>0x1ffc))
            throw std::invalid_argument("Unsupported or malformed NV2A packet");
        for(uint32_t i=0;i<n;++i,p+=4)
            result.push_back({method+(repeated?0:4*i),mem.Read32(uint32_t(p))});
    }
    return result;
}
}
