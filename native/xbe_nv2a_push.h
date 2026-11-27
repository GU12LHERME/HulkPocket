#pragma once
#include "xbox_guest_address_space.h"
#include "gpu/state.h"
#include <bit>
#include <bitset>
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
        case 0x194c:case 0x1950:case 0x195c:case 0x1960:
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
    std::bitset<0x800> combiner_seen{};
    std::array<uint32_t,136*4> program{};
    std::bitset<136*4> program_seen{};
    uint32_t program_word=0,program_start=0,execution_mode=0;
    bool program_load_seen=false,program_start_seen=false,execution_mode_seen=false,context_write=false;
    bool TextureReady(unsigned stage)const{
        return texture_words.at(stage)==3&&texture_dirty.at(stage)&&
            (texture_dirty.at(stage)==3||texture_control.at(stage));
    }
    void TextureInspected(unsigned stage){
        texture_dirty.at(stage)=0;texture_control.at(stage)=false;
    }
};
// NV097 combiner registers, matching XDK D3DPIXELSHADERDEF and nxdk nv_regs.h.
// Stage-program is 0x1e70. Do not confuse it with transform microcode.
inline bool IsCombinerMethod(uint32_t m){
    using namespace gpu;
    auto range=[&](uint32_t b){return m>=b&&m<b+32&&m%4==0;};
    return range(kMPsAlphaInputs)||range(kMPsConstant0)||range(kMPsConstant1)||
           range(kMPsAlphaOutputs)||range(kMPsRgbInputs)||range(kMPsRgbOutputs)||
           m==kMPsFinalAbcd||m==kMPsFinalEfgh||m==kMPsCompareMode||
           m==kMPsFinalConstant0||m==kMPsFinalConstant1||m==kMPsCombinerCount||
           m==0x1e70||m==kMPsDotMapping||m==kMPsInputTexture;
}
inline std::shared_ptr<gpu::PixelShader> CaptureNv2aCombiner(const gpu::State& s,const Nv2aConstantCursor& cursor,uint32_t handle){
    using namespace gpu;
    if(!handle)throw std::invalid_argument("No active guest pixel shader");
    auto get=[&](uint32_t m){
        if(!cursor.combiner_seen.test(m/4))throw std::logic_error("Incomplete NV2A pixel shader state");
        return s.simple[m/4];
    };
    auto shader=std::make_shared<PixelShader>();shader->handle=handle;
    auto* d=shader->def;
    d[53]=get(kMPsCombinerCount);unsigned n=d[53]&255;
    if(n>8)throw std::invalid_argument("Invalid guest combiner count");
    d[8]=get(kMPsFinalAbcd);d[9]=get(kMPsFinalEfgh);
    d[43]=get(kMPsFinalConstant0);d[44]=get(kMPsFinalConstant1);
    d[54]=get(0x1e70);
    // Dependent AR/GB samples read an earlier stage's color. The
    // shared shader translator uses stage zero implicitly for stage one.
    for(unsigned i=0;i<4;++i){
        auto mode=(d[54]>>(5*i))&31;
        if(mode!=0&&mode!=1&&mode!=4&&mode!=5&&mode!=0xf&&mode!=0x10)
            throw std::logic_error("Unintegrated captured pixel texture mode");
        if(mode==5)d[42]=get(kMPsCompareMode);
        if(mode==0xf||mode==0x10){
            if(!i)throw std::logic_error("Dependent pixel stage cannot be first");
            if(i>=2)d[56]=get(kMPsInputTexture);
            unsigned source=i==1?0:(d[56]>>(i==2?16:20))&15;
            if(source>=i)throw std::logic_error("Invalid dependent pixel source stage");
            auto previous=(d[54]>>(5*source))&31;
            if(previous!=1&&previous!=4&&previous!=0xf&&previous!=0x10)
                throw std::logic_error("Dependent pixel source has no color");
        }
    }
    for(unsigned i=0;i<n;++i){
        d[i]=get(kMPsAlphaInputs+4*i);d[26+i]=get(kMPsAlphaOutputs+4*i);
        d[34+i]=get(kMPsRgbInputs+4*i);d[45+i]=get(kMPsRgbOutputs+4*i);
        d[10+i]=get(kMPsConstant0+4*i);d[18+i]=get(kMPsConstant1+4*i);
    }
    // Shared factors must come from stage zero, even when later stages use them.
    if(n){d[10]=get(kMPsConstant0);d[18]=get(kMPsConstant1);}
    return shader;
}
// NV097 transform upload/selection: slots hold four dwords, the upload
// window advances the hardware cursor across packets. Keep prior slots
// when another program is loaded; a shader can select an existing range.
inline std::vector<uint32_t> CaptureNv2aVertexProgram(const Nv2aConstantCursor& c,uint32_t instructions){
    if(!c.execution_mode_seen||c.execution_mode!=2||!c.program_start_seen||c.context_write||
       !instructions||instructions>136||c.program_start>136-instructions)
        throw std::logic_error("Unsupported or incomplete NV2A vertex selection");
    std::vector<uint32_t> result;result.reserve(instructions*4);
    for(uint32_t i=0;i<instructions*4;++i){
        auto pos=c.program_start*4+i;
        if(!c.program_seen.test(pos))throw std::logic_error("Incomplete NV2A vertex program upload");
        auto v=c.program[pos];result.push_back(v);
        if(i%4==3&&bool(v&1)!=(i+1==instructions*4))
            throw std::logic_error("NV2A vertex program termination mismatch");
    }
    return result;
}
// State-only method subset, based on XboxXBERecomp ApplyConstant/SetSimple.
// Entire batch validated before committing; do not silently drop a method.
// Immediate draws, texture commands, reports, and GPU control flow are pending.
inline void ApplyNv2aState(gpu::State& state,Nv2aConstantCursor& cursor,
    const std::vector<Nv2aMethod>& methods,bool capture_combiner=false){
    auto simple=state.simple;
    auto constants=state.vsc;
    auto next=cursor;
    for(auto [m,v]:methods){
        if(capture_combiner){
            if(m==0x1e94){
                if(v!=0&&v!=2&&v!=4)throw std::logic_error("Unsupported NV2A transform execution mode");
                next.execution_mode=v;next.execution_mode_seen=true;simple[m/4]=v;continue;
            }
            if(m==0x1e98){
                if(v)throw std::logic_error("NV2A vertex context writes not integrated");
                next.context_write=false;simple[m/4]=v;continue;
            }
            if(m==0x1e9c||m==0x1ea0){
                if(v>=136)throw std::invalid_argument("NV2A program slot out of range");
                if(m==0x1e9c){next.program_word=v*4;next.program_load_seen=true;}
                else{next.program_start=v;next.program_start_seen=true;}
                simple[m/4]=v;continue;
            }
            if(m>=0xb00&&m<0xb80&&m%4==0){
                if(!next.program_load_seen||next.program_word>=next.program.size())
                    throw std::invalid_argument("NV2A program data without valid load cursor");
                next.program[next.program_word]=v;next.program_seen.set(next.program_word++);
                continue;
            }
        }
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
        if(capture_combiner&&(m==gpu::kMStencilWriteMask||m==gpu::kMStencilFunc||m==gpu::kMStencilRef||
           m==gpu::kMStencilMask||m==gpu::kMStencilZFail||m==gpu::kMStencilPass||
           m==gpu::kMSolidOffsetEnable||m==gpu::kMSlopeScale||m==gpu::kMDepthOffset)){
            simple[m/4]=v;continue;
        }
        if(capture_combiner&&IsCombinerMethod(m)){
            simple[m/4]=v;next.combiner_seen.set(m/4);next.pending_raw_configuration=true;continue;
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
