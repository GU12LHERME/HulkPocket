#pragma once
#include "xbox_guest_address_space.h"
#include "gpu/state.h"
#include <bit>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <string>
namespace hulkpocket {
struct Nv2aMethod {uint32_t method,value;};
// Decode the XDK scratch buffer method packets, as in upstream Walk.
// Reject control-flow packets, nonzero subchannels and truncated input.
// The parser itself does not execute GPU commands.
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

// NV097_SET_TEXGEN_{S,T,R,Q}: stage stride 0x10; zero disables
// automatic coordinates. NV097_SET_TEXTURE_CONTROL0: stage stride
// 0x40; zero disables the hardware sampler and its auxiliary modes.
// Source: XboxDev/nxdk lib/pbkit/nv_regs.h. Nonzero raw values are
// not yet translated into sampler/coordinate-generation semantics.
inline bool IsDisabledTextureMethod(uint32_t m){
    return (m>=0x03c0&&m<=0x03fc&&m%4==0)||
           m==0x1b0c||m==0x1b4c||m==0x1b8c||m==0x1bcc;
}
struct Nv2aConstantCursor {uint32_t reg=0,component=0;bool loaded=false;};
// State-only method subset, based on XboxXBERecomp ApplyConstant/SetSimple.
// Entire batch validated before committing; do not silently drop a method.
// Immediate draws, texture commands, reports, and GPU control flow are pending.
inline void ApplyNv2aState(gpu::State& state,Nv2aConstantCursor& cursor,
    const std::vector<Nv2aMethod>& methods){
    auto simple=state.simple;
    auto constants=state.vsc;
    auto next=cursor;
    for(auto [m,v]:methods){
        if(m==0x1ea4){
            if(v>=constants.size())throw std::invalid_argument("NV2A constant index out of range");
            next={v,0,true};continue;
        }
        if(m>=0xb80&&m<0xc00){
            if(!next.loaded||next.reg>=constants.size()||next.component>=4)
                throw std::invalid_argument("NV2A constant data without valid register");
            constants[next.reg][next.component++]=std::bit_cast<float>(v);
            if(next.component==4){next.component=0;++next.reg;}
            continue;
        }
        if(IsDisabledTextureMethod(m)){
            if(v)throw std::logic_error("Enabled raw NV2A texture control/texgen not integrated");
            simple[m>>2]=v;continue;
        }
        switch(m){
            case gpu::kMBlendEnable:case gpu::kMSrcBlend:case gpu::kMDestBlend:
            case gpu::kMBlendOp:case gpu::kMZFunc:case gpu::kMColorWriteEnable:
            case gpu::kMAlphaTestEnable:case gpu::kMAlphaFunc:case gpu::kMAlphaRef:
            case gpu::kMZWriteEnable:
                simple[m>>2]=v;break;
            default:{
                char detail[96];
                std::snprintf(detail,sizeof(detail),"NV2A method not integrated: %04X=%08X",m,v);
                throw std::logic_error(detail);
            }
        }
    }
    state.simple=simple;state.vsc=constants;cursor=next;
}
}
