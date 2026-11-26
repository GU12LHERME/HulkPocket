#pragma once
// Portable subset of XboxXBERecomp's D3D8 state transitions.
// This is the graphics-side bridge, not yet a guest hook replacement:
// device mirrors, reference ownership and pushbuffer walking belong to the next layer.
#include "gpu/backend.h"
#include "xbe_nv2a_push.h"
#include "xbe_nv2a_texture.h"
#include "gpu/guest_memory.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "cpu/x86_context.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string_view>

namespace hulkpocket {
enum class D3dArg : uint8_t { Stack,Eax,Ecx,Edx,Ebx,Esi,Edi,Ebp };
struct D3dAbi {
    std::array<D3dArg,16> slots{};
    unsigned count=0;
    static D3dAbi Stdcall(unsigned n) {
        if(n>16)throw std::invalid_argument("D3D ABI too many arguments");
        D3dAbi result;result.count=n;return result;
    }
    static D3dAbi Mapped(std::initializer_list<D3dArg> args) {
        if(args.size()>16)throw std::invalid_argument("D3D ABI too many arguments");
        D3dAbi result;result.count=unsigned(args.size());
        std::copy(args.begin(),args.end(),result.slots.begin());return result;
    }
};
class XbeD3d8Bridge {
    XboxGuestAddressSpace& mem_;
    gpu::Backend& backend_;
    gpu::State state_{};
    Nv2aConstantCursor push_constants_{};
    std::mutex lock_;
    bool initialized_=false;
    uint32_t presents_=0;
    static float Float(uint32_t x){return std::bit_cast<float>(x);}
    uint32_t Read(uint64_t a){return gpu::ReadGuest32(a);}
    gpu::Surface Surface(uint32_t h) {
        if(!h)return {};
        gpu::CheckedGuestRange(h,20);
        if((Read(h)&0x70000)!=0x50000)throw std::invalid_argument("Not an Xbox surface");
        return {h,Read(uint64_t(h)+4),Read(uint64_t(h)+12),Read(uint64_t(h)+16)};
    }
    void ViewportConstants() {
        auto& v=state_.viewport;
        auto f=(state_.zs.format>>8)&255u;
        state_.zscale=f==0x2c||f==0x30?65535.0f:16777215.0f;
        float hw=v.w*0.5f,hh=v.h*0.5f;
        state_.vsc[58]={hw,-hh,(v.max_z-v.min_z)*state_.zscale,0};
        state_.vsc[59]={v.x+state_.ss_offset_x+hw,v.y+state_.ss_offset_y+hh,v.min_z*state_.zscale,0};
    }
    static void Count(std::span<const uint32_t> a,unsigned n) {
        if(a.size()!=n)throw std::invalid_argument("Wrong D3D8 API argument count");
    }
    static uint32_t Register(const X86Context& c,D3dArg r) {
        switch(r) {
            case D3dArg::Eax:return c.eax;case D3dArg::Ecx:return c.ecx;
            case D3dArg::Edx:return c.edx;case D3dArg::Ebx:return c.ebx;
            case D3dArg::Esi:return c.esi;case D3dArg::Edi:return c.edi;
            case D3dArg::Ebp:return c.ebp;default:throw std::invalid_argument("Invalid D3D register");
        }
    }
    uint32_t Execute(std::string_view name,std::span<const uint32_t> a) {
        if(!initialized_)throw std::logic_error("D3D backend not initialized");
        if(name=="D3DDevice_SetViewport") {
            Count(a,1);
            if(a[0]) {
                auto p=uint64_t(a[0]);gpu::CheckedGuestRange(p,24);
                gpu::Viewport v{Read(p),Read(p+4),Read(p+8),Read(p+12),Float(Read(p+16)),Float(Read(p+20))};
                if(!v.w||!v.h||uint64_t(v.x)+v.w>4096||uint64_t(v.y)+v.h>4096||
                   !std::isfinite(v.min_z)||!std::isfinite(v.max_z)||
                   v.min_z<0||v.max_z>1||v.min_z>v.max_z)
                    throw std::invalid_argument("Invalid D3D viewport");
                state_.viewport=v;
            }
            state_.scissor_count=0;ViewportConstants();return 0;
        }
        if(name=="D3DDevice_SetScissors") {
            Count(a,3);
            if(a[1])throw std::invalid_argument("Exclusive scissors not integrated");
            if(a[0]>8)throw std::invalid_argument("Too many scissors");
            if(a[0])gpu::CheckedGuestRange(a[2],uint64_t(a[0])*16);
            for(uint32_t i=0;i<a[0]*4;++i)state_.scissors[i]=Read(uint64_t(a[2])+i*4);
            state_.scissor_count=a[0];return 0;
        }
        if(name=="D3DDevice_SetScreenSpaceOffset") {
            Count(a,2);
            if(!std::isfinite(Float(a[0]))||!std::isfinite(Float(a[1])))
                throw std::invalid_argument("Invalid screen offset");
            state_.ss_offset_x=Float(a[0]);state_.ss_offset_y=Float(a[1]);ViewportConstants();return 0;
        }
        if(name=="D3DDevice_SetRenderState_Simple") {
            Count(a,2);state_.simple[(a[0]&0x1ffc)>>2]=a[1];return 0;
        }
        if(name=="D3DDevice_SetRenderState_ZEnable") {
            Count(a,1);
            if(a[0]>1)throw std::invalid_argument("W-buffer not integrated");
            state_.rs[gpu::kRsZEnable]=a[0];return 0;
        }
        if(name=="D3DDevice_SetRenderState_StencilEnable"||name=="D3DDevice_SetRenderState_StencilFail"){
            Count(a,1);
            if(name=="D3DDevice_SetRenderState_StencilEnable"&&a[0]>1)
                throw std::invalid_argument("Invalid stencil enable");
            state_.rs[name=="D3DDevice_SetRenderState_StencilEnable"?gpu::kRsStencilEnable:gpu::kRsStencilFail]=a[0];
            return 0;
        }
        if(name=="D3DDevice_SetTextureStageStateNotInline") {
            Count(a,3);
            if(a[1]>=gpu::kTssCount)throw std::invalid_argument("Unknown texture stage state");
            state_.tss[a[0]&3][a[1]]=a[2];return 0;
        }
        if(name=="D3DDevice_SetTransform") {
            Count(a,2);auto t=a[0];if(t>=10)throw std::invalid_argument("Unknown Xbox transform");
            auto pc=t<2?t+2:t<6?t+14:t+250;
            std::memcpy(state_.transforms[pc].data(),gpu::CheckedGuestRange(a[1],64),64);return 0;
        }
        if(name=="D3DDevice_SetMaterial") {
            Count(a,1);std::memcpy(state_.material.data(),gpu::CheckedGuestRange(a[0],68),68);return 0;
        }
        if(name=="D3DDevice_SetTexture"||name=="D3DDevice_SetPalette") {
            Count(a,2);
            if(a[1])gpu::CheckedGuestRange(a[1],name=="D3DDevice_SetTexture"?20:8);
            auto& target=name=="D3DDevice_SetTexture"?state_.textures:state_.palettes;
            target[a[0]&3]=a[1];return 0;
        }
        if(name=="D3DDevice_SetStreamSource") {
            Count(a,3);
            if(a[1])gpu::CheckedGuestRange(a[1],8);
            if(a[1]&&(!a[2]||a[2]>65536))throw std::invalid_argument("Invalid vertex stride");
            state_.streams[a[0]&15]={a[1],a[2]};return 0;
        }
        if(name=="D3DDevice_SetIndices") {
            Count(a,2);
            auto data=a[0]?Read(uint64_t(a[0])+4):0;
            state_.index_buffer=a[0];state_.index_data=data;state_.base_vertex=a[1];return 0;
        }
        if(name=="D3DDevice_SetVertexShader") {
            Count(a,1);
            if(a[0]&1)throw std::logic_error("Programmable vertex shader creation not integrated");
            state_.vs_handle=a[0];state_.vs.reset();return 0;
        }
        if(name=="D3DDevice_SetPixelShaderConstant") {
            Count(a,3);
            if(a[0]>16||a[2]>16-a[0])throw std::invalid_argument("Invalid pixel constants");
            if(a[2]) {
                auto* p=gpu::CheckedGuestRange(a[1],uint64_t(a[2])*16);
                for(uint32_t i=0;i<a[2];++i)std::memcpy(state_.psc[a[0]+i].data(),p+i*16,16);
            }
            gpu::ApplyPixelConstants(state_,a[0],a[2]);return 0;
        }
        if(name=="D3DDevice_SetRenderTarget") {
            Count(a,2);auto color=Surface(a[0]),depth=Surface(a[1]);
            state_.rt=color;state_.zs=depth;ViewportConstants();return 0;
        }
        if(name=="D3DDevice_Clear") {
            Count(a,6);
            if(!state_.rt.header)throw std::logic_error("No color render target");
            if(a[0]>4096)throw std::invalid_argument("Too many clear rectangles");
            auto* rects=a[0]?gpu::CheckedGuestRange(a[1],uint64_t(a[0])*16):nullptr;
            backend_.Clear(state_,a[0],rects,a[2],a[3],Float(a[4]),a[5]);return 0;
        }
        if(name=="D3DDevice_DrawVertices"||name=="D3DDevice_DrawIndexedVertices") {
            if(push_constants_.pending_raw_configuration)
                throw std::logic_error("Raw NV2A clip/viewport/bump/shader state requires draw translation");
            Count(a,3);
            if(!state_.rt.header)throw std::logic_error("No color render target");
            bool indexed=name=="D3DDevice_DrawIndexedVertices";
            auto n=indexed?a[1]:a[2];
            if(n>262144||a[0]<1||a[0]>10)throw std::invalid_argument("Invalid Xbox draw range");
            if(!n)return 0;
            const uint8_t* indices=indexed?gpu::CheckedGuestRange(a[2],uint64_t(n)*2):nullptr;
            backend_.Draw(state_,a[0],indexed?0:a[1],n,indices);return 0;
        }
        // Swap is not intercepted: the XDK device layer must rotate buffers,
        // process pending pushes and update the guest-visible swap counter.
        throw std::logic_error("Unsupported D3D8 bridge operation");
    }
public:
    XbeD3d8Bridge(XboxGuestAddressSpace& mem,gpu::Backend& backend):mem_(mem),backend_(backend){}
    void Initialize(uint32_t w,uint32_t h) {
        std::lock_guard guard(lock_);
        if(initialized_)throw std::logic_error("D3D bridge already initialized");
        if(!w||!h||w>4096||h>4096)throw std::invalid_argument("Invalid framebuffer size");
        gpu::ScopedGuestMemory scope(mem_);
        if(!backend_.Init(w,h))throw std::runtime_error("D3D backend initialization failed");
        state_.viewport={0,0,w,h,0,1};
        // Initial identity transforms and ARGB write mask for this standalone bridge.
        // The actual game's XDK default state must replace these on device creation.
        for(auto slot:{2,3,256})for(unsigned i=0;i<4;++i)state_.transforms[slot][i*5]=1.0f;
        state_.simple[gpu::kMColorWriteEnable>>2]=0x01010101;
        ViewportConstants();initialized_=true;
    }
    // Bind actual allocated device surfaces before any guest draw.
    // Does not change guest registers or invoke unimplemented default states.
    void BindInitialTargets(uint32_t color,uint32_t depth){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        if(!initialized_||!color)throw std::logic_error("Invalid initial render targets");
        auto rt=Surface(color),zs=Surface(depth);
        state_.rt=rt;state_.zs=zs;ViewportConstants();
    }
    // Transfer the real initializer's state into the live bridge. The observed
    // NV2A packets are retained verbatim in the method-indexed state; storing
    // them is not a claim that the GLES draw path implements their semantics.
    // Reject any new packet rather than silently treating it as supported.
    void AdoptOriginalDefaults(const gpu::State& captured,uint32_t begin,uint32_t end){
        std::lock_guard guard(lock_);
        if(!initialized_||!state_.rt.header)throw std::logic_error("D3D targets not initialized");
        auto methods=DecodeNv2aMethods(mem_,begin,end);
        auto next=captured.simple;
        for(auto [m,v]:methods){
            switch(m){
                case 0x1b0c:case 0x1b4c:case 0x1b8c:case 0x1bcc:
                case 0x0a20:case 0x0a24:case 0x0a28:case 0x0a2c:
                case 0x0394:case 0x0398:next[m>>2]=v;break;
                default:throw std::logic_error("Unrecognized original default packet");
            }
        }
        state_.simple=next;state_.rs=captured.rs;state_.tss=captured.tss;
    }
    // Transfer state-only setter updates without resetting render targets,
    // streams, shader constants or the pending NV2A constant cursor.
    void AdoptLiveSetters(const gpu::State& captured){
        std::lock_guard guard(lock_);
        if(!initialized_||!state_.rt.header)throw std::logic_error("D3D targets not initialized");
        state_.simple=captured.simple;state_.rs=captured.rs;state_.tss=captured.tss;
    }
    // XDK 5849 SyncDeferred plus the verified 12-byte stream table.
    // Read all ranges before committing. Do not infer a shader handle
    // from the CDevice shader object address.
    void SyncGuestDrawTables(uint32_t streams,uint32_t render,uint32_t texture){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        if(!initialized_||!streams||!render||!texture)
            throw std::invalid_argument("Missing generated draw tables");
        gpu::CheckedGuestRange(streams,16*12);
        gpu::CheckedGuestRange(render,gpu::kRsComplexFirst*4);
        gpu::CheckedGuestRange(texture,4*gpu::kTssCount*4);
        auto next=state_;
        for(uint32_t i=gpu::kRsDeferredFirst;i<gpu::kRsComplexFirst;++i)
            next.rs[i]=Read(uint64_t(render)+i*4);
        for(unsigned st=0;st<4;++st)
            for(unsigned i=0;i<gpu::kTssBumpEnvMat00;++i)
                next.tss[st][i]=Read(uint64_t(texture)+(st*gpu::kTssCount+i)*4);
        for(unsigned i=0;i<16;++i){
            auto base=uint64_t(streams)+12*i;
            next.streams[i]={Read(base+8),Read(base)};
            if(next.streams[i].vb){
                gpu::CheckedGuestRange(next.streams[i].vb,12);
                if(!next.streams[i].stride||next.streams[i].stride>65536)
                    throw std::invalid_argument("Invalid guest stream stride");
            }
        }
        state_=std::move(next);
    }
    // XDK 5849 shader object layout corroborated by Cxbx-Reloaded
    // X_D3DVertexShader / X_VERTEXSHADERINPUT. Accept only a declaration
    // exactly equivalent to an already-supported untextured FVF. The
    // actual flags, stream, offset and format of all 16 slots are checked.
    uint32_t SyncFixedDiffuseShader(uint32_t device){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        gpu::CheckedGuestRange(device,0x24a0);
        auto ptr=Read(uint64_t(device)+0x794);
        if(!ptr||ptr%4)throw std::invalid_argument("Missing guest vertex shader object");
        gpu::CheckedGuestRange(ptr,0x14+16*16);
        if(Read(uint64_t(ptr)+4)!=0x400||Read(uint64_t(ptr)+8)||Read(uint64_t(ptr)+12)||
           Read(uint64_t(device)+0x784))
            throw std::logic_error("Unsupported guest shader program or flags");
        for(unsigned st=0;st<4;++st)
            if(Read(uint64_t(device)+0xf88+4*st)||Read(uint64_t(device)+0xf98+4*st))
                throw std::logic_error("Fixed diffuse shader requires unbound textures and palettes");
        uint32_t fvf=0;
        for(uint32_t candidate:{0x42u,0x44u}){
            gpu::VertexShader layout;
            gpu::FvfInputs(candidate,layout);
            bool match=true;
            for(unsigned i=0;i<16;++i){
                auto slot=uint64_t(ptr)+0x14+16*i;
                auto type=Read(slot+8);
                const auto& want=layout.inputs[i];
                if(want.stream<0){
                    if(type!=0&&type!=2)match=false;
                }else if(type!=want.type||Read(slot)!=uint32_t(want.stream)||
                    Read(slot+4)!=want.offset||Read(slot+12)&0xffffu)match=false;
            }
            if(match&&state_.streams[0].vb&&state_.streams[0].stride==layout.stream_stride[0]){
                fvf=candidate;break;
            }
        }
        if(!fvf)throw std::logic_error("Guest fixed vertex declaration not equivalent to supported diffuse FVF");
        auto next=state_;
        next.vs_handle=fvf;next.vs.reset();next.ps.reset();
        next.textures.fill(0);next.palettes.fill(0);
        next.base_vertex=Read(uint64_t(device)+0x1c);
        // Copy the actual matrices instead of using the standalone identity.
        for(unsigned tr:{0u,1u,6u}){
            auto pc=tr<2?tr+2:tr+250;
            std::memcpy(next.transforms[pc].data(),gpu::CheckedGuestRange(uint64_t(device)+0xc60+64*tr,64),64);
            for(float v:next.transforms[pc])if(!std::isfinite(v))
                throw std::invalid_argument("Nonfinite guest transform");
        }
        auto vp=uint64_t(device)+0xee0;
        next.viewport={Read(vp),Read(vp+4),Read(vp+8),Read(vp+12),Float(Read(vp+16)),Float(Read(vp+20))};
        next.ss_offset_x=Float(Read(vp+24))-0.53125f;
        next.ss_offset_y=Float(Read(vp+28))-0.53125f;
        if(!std::isfinite(next.viewport.min_z)||!std::isfinite(next.viewport.max_z)||
           !std::isfinite(next.ss_offset_x)||!std::isfinite(next.ss_offset_y))
            throw std::invalid_argument("Nonfinite guest viewport");
        state_=std::move(next);ViewportConstants();return fvf;
    }
    // Prove that the real stream can be assembled, even if later raster
    // translation must stop. No backend draw or guest return is implied.
    uint32_t VerifyGuestGeometry(uint32_t prim,uint32_t start,uint32_t count,uint32_t indices){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        if(!count||count>262144||prim<1||prim>10)
            throw std::invalid_argument("Invalid guest geometry request");
        auto* ip=indices?gpu::CheckedGuestRange(indices,uint64_t(count)*2):nullptr;
        gpu::Assembled a;
        if(!gpu::Assemble(state_,prim,start,count,ip,a)||a.regs!=9)
            throw std::runtime_error("Guest diffuse geometry assembly failed");
        return a.vertex_count;
    }
    // Reduced diagnostic line profile. Use the actual guest viewport if
    // valid; zero extent fallback is for investigation only. Keep guest
    // state and the normal unresolved raw-packet guard intact.
    // Match the already observed reduced line profile before permitting
    // another diagnostic return. Only the stream bindings and vertex data
    // may change; no assumption that a different raster state is harmless.
    static bool SameDiagnosticLineProfile(const gpu::State& a,const gpu::State& b){
        return a.rs==b.rs&&a.tss==b.tss&&a.simple==b.simple&&
            a.textures==b.textures&&a.palettes==b.palettes&&
            a.vs_handle==b.vs_handle&&!a.vs&&!b.vs&&!a.ps&&!b.ps&&
            a.rt.header==b.rt.header&&a.zs.header==b.zs.header&&
            a.base_vertex==b.base_vertex&&a.scissor_count==b.scissor_count&&
            a.scissors==b.scissors&&a.transforms==b.transforms&&
            a.viewport.x==b.viewport.x&&a.viewport.y==b.viewport.y&&
            a.viewport.w==b.viewport.w&&a.viewport.h==b.viewport.h&&
            a.viewport.min_z==b.viewport.min_z&&a.viewport.max_z==b.viewport.max_z&&
            a.ss_offset_x==b.ss_offset_x&&a.ss_offset_y==b.ss_offset_y&&a.zscale==b.zscale;
    }
    void ReplayFixedLinesDiagnostic(uint32_t start,uint32_t count){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        if(!initialized_||state_.vs_handle!=0x42||state_.vs||state_.ps||
           !count||count>262144||count%2||
           ((state_.viewport.w==0)!=(state_.viewport.h==0))||
           state_.viewport.x||state_.viewport.y||state_.viewport.min_z!=0||
           state_.viewport.max_z!=1||state_.ss_offset_x||state_.ss_offset_y||
           state_.rs[gpu::kRsLighting]||state_.rs[gpu::kRsFogEnable]||
           state_.rs[gpu::kRsSpecularEnable]||state_.rs[gpu::kRsVertexBlend]||
           state_.rs[gpu::kRsStencilEnable]||state_.scissor_count||
           std::any_of(state_.textures.begin(),state_.textures.end(),[](auto p){return p!=0;})||
           std::any_of(state_.palettes.begin(),state_.palettes.end(),[](auto p){return p!=0;}))
            throw std::logic_error("Unsupported diagnostic line replay profile");
        gpu::TextureDesc desc{};
        if(!gpu::DescribeSurface(state_.rt,desc)||!desc.width||!desc.height)
            throw std::invalid_argument("Missing replay target extent");
        auto replay=state_;
        if(!replay.viewport.w&&!replay.viewport.h)
            replay.viewport={0,0,desc.width,desc.height,0,1};
        else if(replay.viewport.w!=desc.width||replay.viewport.h!=desc.height)
            throw std::logic_error("Diagnostic line profile requires full target viewport");
        // Reduced fixed-function profile: texture sampling, lighting, fog,
        // stencil and polygon culling are inactive for these unlit lines.
        // This does not translate hardware clip, multisample or raster hints.
        replay.rs.fill(0);replay.tss={};replay.simple.fill(0);
        replay.rs[gpu::kRsZEnable]=state_.rs[gpu::kRsZEnable];
        for(auto m:{gpu::kMAlphaTestEnable,gpu::kMAlphaFunc,gpu::kMAlphaRef,
                    gpu::kMBlendEnable,gpu::kMSrcBlend,gpu::kMDestBlend,gpu::kMBlendOp,
                    gpu::kMZFunc,gpu::kMZWriteEnable,gpu::kMColorWriteEnable,
                    gpu::kMDitherEnable,gpu::kMShadeMode})
            replay.simple[m>>2]=state_.simple[m>>2];
        backend_.Draw(replay,2,start,count,nullptr);
        backend_.Flush();
    }
    void ApplyPush(uint32_t begin,uint32_t end){
        std::lock_guard guard(lock_);
        if(!initialized_)throw std::logic_error("D3D bridge not initialized");
        // Validate and decode newly complete raw texture bindings before
        // committing the scratch batch. Do not enable a draw just because
        // the image data decodes; sampler, shader and raster state still matter.
        auto methods=DecodeNv2aMethods(mem_,begin,end);
        auto next=state_;auto cursor=push_constants_;
        ApplyNv2aState(next,cursor,methods);
        gpu::ScopedGuestMemory scope(mem_);
        std::array<uint8_t,4> changed{};
        for(auto [m,v]:methods){
            (void)v;
            if(m>=0x1b00&&m<0x1c00&&(m&0x3f)<=4)
                changed[(m-0x1b00)/0x40]|=(m&0x3f)==0?1u:2u;
        }
        for(unsigned stage=0;stage<4;++stage){
            if(changed[stage]!=3)continue;
            const auto base=0x1b00+stage*0x40;
            auto offset=next.simple[base>>2],format=next.simple[(base+4)>>2];
            // Only decode the verified swizzled 2D family here; preserve
            // all other bindings for explicit validation at the draw boundary.
            if((format&3)==1&&!(format&4)&&((format>>4)&15)==2&&
               gpu::Format((format>>8)&255).swizzled&&
               gpu::Format((format>>8)&255).host==gpu::Host::BGRA8&&((format>>8)&255)!=0xb){
                auto probe=ProbeNv2aSwizzledTexture(offset,format);
                std::fprintf(stderr,"D3D RAW TEXTURE DECODE PASS stage=%u physical=%08X format=%08X %ux%u mips=%u hash=%016llX (not sampled)\n",
                    stage,offset,format,probe.desc.width,probe.desc.height,probe.desc.levels,
                    static_cast<unsigned long long>(probe.hash));
            }else{
                std::fprintf(stderr,"D3D RAW TEXTURE PENDING stage=%u physical=%08X format=%08X\n",stage,offset,format);
            }
        }
        state_=std::move(next);push_constants_=cursor;
    }
    uint32_t Call(std::string_view name,X86Context& cpu,const D3dAbi& abi) {
        std::lock_guard guard(lock_);
        gpu::ScopedGuestMemory scope(mem_);
        if(abi.count>abi.slots.size())throw std::invalid_argument("Invalid D3D ABI");
        uint32_t stack_count=0;
        for(unsigned i=0;i<abi.count;++i)if(abi.slots[i]==D3dArg::Stack)++stack_count;
        const uint64_t end=uint64_t(cpu.esp)+4+stack_count*4;
        if(end>UINT32_MAX)throw std::out_of_range("D3D guest stack overflow");
        gpu::CheckedGuestRange(cpu.esp,4+stack_count*4);
        std::array<uint32_t,16> args{};
        unsigned j=0;
        for(unsigned i=0;i<abi.count;++i)args[i]=abi.slots[i]==D3dArg::Stack?
            Read(uint64_t(cpu.esp)+4+4*j++):Register(cpu,abi.slots[i]);
        auto result=Execute(name,std::span<const uint32_t>(args.data(),abi.count));
        cpu.eax=result;cpu.esp=uint32_t(end);return result;
    }
    // Synchronous diagnostic timeline: only the initial epoch is known.
    // Original timestamps cannot be compared to unrelated GLES fence IDs.
    void CompleteInitialGpuWait(uint32_t target,uint32_t current,uint32_t flags,uint32_t callback){
        std::lock_guard guard(lock_);
        if(!initialized_||target||current||callback||(flags!=1&&flags!=2&&flags!=4))
            throw std::logic_error("Unsupported D3D wait timeline or callback");
        backend_.Flush(); // GLES implementation performs glFinish + error check.
    }
    // Explicit present endpoint, called only by the owning device layer.
    uint32_t Present(uint32_t surface) {
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        if(!initialized_||!surface)throw std::logic_error("No initialized present surface");
        backend_.Present(Surface(surface));return ++presents_;
    }
    gpu::State Snapshot(){std::lock_guard guard(lock_);return state_;}
};
}
