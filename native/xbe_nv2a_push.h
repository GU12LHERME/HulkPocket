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
// NV097 state registers verified against nxdk nv_regs.h. These are
// configuration writes, not draw/clear/transfer commands. Preserve the
// hardware words, including zero clip values, without claiming a GLES
// translation. The bridge must stop before drawing until they are translated.
inline bool IsPendingRawConfiguration(uint32_t m){
    if(m>=0x1b28&&m<=0x1bfc&&m%4==0&&(m&0x3f)>=0x28)return true;
    switch(m){
        case 0x0200:case 0x0204: // surface clip
        case 0x02b4:case 0x02c0:case 0x02e0: // window clip type and first rectangle
        case 0x0394:case 0x0398: // depth clip min/max
        case 0x0a20:case 0x0a24:case 0x0a28:case 0x0a2c: // viewport offset
        // Packed current vertex attributes (NV097_SET_VERTEX_DATA4UB).
        // Retain values; their use by missing shader inputs is not translated.
        case 0x1950:case 0x195c:case 0x1960:
        case 0x1e78: // shader other-stage input
            return true;
        default:return false;
    }
}
struct Nv2aConstantCursor {
    uint32_t reg=0,component=0;
    bool loaded=false,pending_raw_configuration=false;
    // Offset and format may arrive in separate packets. Retain per-stage
    // presence independently of their values (physical offset zero is legal).
    std::array<uint8_t,4> texture_words{}, texture_dirty{};
    std::array<bool,4> texture_control{};
    bool TextureReady(unsigned stage)const{
        return texture_words.at(stage)==3&&texture_dirty.at(stage)&&
            (texture_dirty.at(stage)==3||texture_control.at(stage));
    }
    void TextureInspected(unsigned stage){
        texture_dirty.at(stage)=0;texture_control.at(stage)=false;
    }
};
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
            next.reg=v;next.component=0;next.loaded=true;continue;
        }
        if(m>=0xb80&&m<0xc00){
            if(!next.loaded||next.reg>=constants.size()||next.component>=4)
                throw std::invalid_argument("NV2A constant data without valid register");
            constants[next.reg][next.component++]=std::bit_cast<float>(v);
            if(next.component==4){next.component=0;++next.reg;}
            continue;
        }
        // NV097_BREAK_VERTEX_BUFFER_CACHE. The synchronous host assembler
        // rereads guest vertex memory for each draw and has no vertex cache.
        if(m==0x1710){
            if(v)throw std::logic_error("Unsupported vertex cache break argument");
            continue;
        }
        if(IsPendingRawConfiguration(m)){
            simple[m>>2]=v;next.pending_raw_configuration=true;continue;
        }
        // NV097 texture registers, four units at stride 0x40. Keep
        // actual words and presence; resource decoding happens in the bridge.
        // These are state writes, not proof of a translated sampler.
        if(m>=0x1b00&&m<0x1c00){
            auto stage=(m-0x1b00)/0x40,off=m&0x3f;
            if(off==0||off==4){
                const uint8_t bit=off==0?1u:2u;
                next.texture_words[stage]|=bit;
                next.texture_dirty[stage]|=bit;
                simple[m>>2]=v;next.pending_raw_configuration=true;continue;
            }
            if(off==8||off==0x10||off==0x14||off==0x1c||off==0x20||off==0x24){
                simple[m>>2]=v;next.pending_raw_configuration=true;continue;
            }
            if(off==0xc){
                if(v&&next.texture_words[stage]!=3)
                    throw std::logic_error("Enabled raw texture without offset and format");
                if(next.texture_words[stage]==3&&next.texture_dirty[stage])
                    next.texture_control[stage]=true;
                if(v)next.pending_raw_configuration=true;
                simple[m>>2]=v;continue;
            }
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
