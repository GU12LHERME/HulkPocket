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
    bool defer_draw_flush_=false;
    gpu::State state_{};
    Nv2aConstantCursor push_constants_{};
    std::mutex lock_;
    bool initialized_=false;
    bool material_captured_=false;
    uint32_t light_defined_=0,light_enable_seen_=0;
    uint32_t presents_=0,framebuffer_w_=0,framebuffer_h_=0;
    static float Float(uint32_t x){return std::bit_cast<float>(x);}
    static bool SupportedVulkanFixedFvf(uint32_t fvf){
        const auto pos=fvf&0xe,uv=(fvf>>8)&15;
        if((pos!=2&&pos!=4)||uv>4||(fvf&0x0000f029u)||(fvf>>24)||
           (pos==4&&(fvf&0x10)))return false;
        const uint32_t used=uv?((1u<<(uv*2))-1):0;
        return !(fvf>>16&~used);
    }
    uint32_t Read(uint64_t a){return gpu::ReadGuest32(a);}
    gpu::Surface Surface(uint32_t h) {
        if(!h)return {};
        gpu::CheckedGuestRange(h,20);
        if((Read(h)&0x70000)!=0x50000)throw std::invalid_argument("Not an Xbox surface");
        return {h,Read(uint64_t(h)+4),Read(uint64_t(h)+12),Read(uint64_t(h)+16)};
    }
    void ViewportConstants(bool preserve_captured=false) {
        auto& v=state_.viewport;
        auto f=(state_.zs.format>>8)&255u;
        state_.zscale=f==0x2c||f==0x30?65535.0f:16777215.0f;
        float hw=v.w*0.5f,hh=v.h*0.5f;
        const std::array<std::array<float,4>,2> defaults{{
            {hw,-hh,(v.max_z-v.min_z)*state_.zscale,0},
            {v.x+state_.ss_offset_x+hw,v.y+state_.ss_offset_y+hh,v.min_z*state_.zscale,0}}};
        for(unsigned r=0;r<2;++r)for(unsigned c=0;c<4;++c)
            if(!preserve_captured||!(push_constants_.viewport_constant_seen[r]&(1u<<c)))
                state_.vsc[58+r][c]=defaults[r][c];
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
        if(name=="D3DDevice_CopyRects") {
            Count(a,5);
            if(!a[0]||!a[3])throw std::invalid_argument("Missing D3D copy surface");
            backend_.Copy(Surface(a[0]),Surface(a[3]),a[2],a[1],a[4],1.f,1.f);return 0;
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
    XbeD3d8Bridge(XboxGuestAddressSpace& mem,gpu::Backend& backend,bool defer_draw_flush=false)
        :mem_(mem),backend_(backend),defer_draw_flush_(defer_draw_flush){}
    void Initialize(uint32_t w,uint32_t h) {
        std::lock_guard guard(lock_);
        if(initialized_)throw std::logic_error("D3D bridge already initialized");
        if(!w||!h||w>4096||h>4096)throw std::invalid_argument("Invalid framebuffer size");
        gpu::ScopedGuestMemory scope(mem_);
        if(!backend_.Init(w,h))throw std::runtime_error("D3D backend initialization failed");
        framebuffer_w_=w;framebuffer_h_=h;
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
    // Read the guest-owned device mirrors without inventing a SetRenderTarget
    // return or touching the title's resource reference counts.
    // Restrict the live Vulkan path to the existing backend's same-size
    // linear color and depth support, including a null depth attachment.
    void SyncVulkanTargetMirrors(uint32_t device){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        if(!initialized_||!device)throw std::logic_error("Invalid live Vulkan device");
        gpu::CheckedGuestRange(uint64_t(device)+0x1a04,8);
        auto color=Read(uint64_t(device)+0x1a04),depth=Read(uint64_t(device)+0x1a08);
        auto rt=Surface(color),zs=Surface(depth);
        if(!color||Read(uint64_t(color)+8)||(depth&&Read(uint64_t(depth)+8)))
            throw std::logic_error("Invalid or locked live render target");
        auto check=[&](const gpu::Surface& s,uint32_t format){
            gpu::TextureDesc d{};
            if(!gpu::DescribeSurface(s,d)||!d.linear||d.format!=format||
               d.depth!=1||d.levels!=1||d.width!=framebuffer_w_||d.height!=framebuffer_h_||
               d.pitch<uint64_t(d.width)*4)
                throw std::logic_error("Unsupported live Vulkan surface layout");
            gpu::CheckedGuestRange(d.data,uint64_t(d.pitch)*d.height);
            return d;
        };
        const auto c=check(rt,0x12);
        if(depth){
            const auto z=check(zs,0x2e);
            if(uint64_t(c.data)<uint64_t(z.data)+uint64_t(z.pitch)*z.height&&
               uint64_t(z.data)<uint64_t(c.data)+uint64_t(c.pitch)*c.height)
                throw std::logic_error("Live Vulkan color/depth overlap");
        }
        if(color!=state_.rt.header||depth!=state_.zs.header)
            std::fprintf(stderr,"D3D VULKAN TARGET CHANGE color=%08X depth=%08X\n",color,depth);
        state_.rt=rt;state_.zs=zs;ViewportConstants(true);
    }
    // Validate captured surface register writes against the guest-owned
    // mirrors, never using raw offsets as unchecked host pointers.
    // Consume only successfully matched writes; verified swap changes the
    // backing independently, so old writes must not be replayed as new ones.
    void ValidateCapturedTargets(){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        const auto pending=push_constants_.surface_pending;
        if(!pending)return;
        if(!initialized_)throw std::logic_error("Surface configuration before device init");
        gpu::TextureDesc color{},depth{};
        if(!gpu::DescribeSurface(state_.rt,color)||!color.linear||color.format!=0x12||
           color.depth!=1||color.levels!=1||!color.width||!color.height||
           color.pitch<uint64_t(color.width)*4)
            throw std::logic_error("Captured NV2A color surface disagrees with guest layout");
        gpu::CheckedGuestRange(color.data,uint64_t(color.pitch)*color.height);
        const bool has_depth=state_.zs.header!=0;
        if(has_depth){
            if(!gpu::DescribeSurface(state_.zs,depth)||!depth.linear||depth.format!=0x2e||
               depth.depth!=1||depth.levels!=1||depth.width!=color.width||depth.height!=color.height||
               depth.pitch<uint64_t(depth.width)*4)
                throw std::logic_error("Captured NV2A depth surface disagrees with guest layout");
            gpu::CheckedGuestRange(depth.data,uint64_t(depth.pitch)*depth.height);
        }
        const auto& r=state_.simple;
        if((pending&1)&&has_depth&&((r[0x0208/4]>>4)&15)!=2)
            throw std::logic_error("NV2A depth format disagrees with guest target");
        if((pending&2)&&((r[0x020c/4]&0xffffu)!=color.pitch||
           (has_depth&&(r[0x020c/4]>>16)!=depth.pitch)))
            throw std::logic_error("NV2A surface pitch disagrees with guest target");
        if((pending&4)&&gpu::PhysicalResourceAddress(r[0x0210/4])!=color.data)
            throw std::logic_error("NV2A color offset disagrees with guest target");
        if((pending&8)&&((has_depth&&gpu::PhysicalResourceAddress(r[0x0214/4])!=depth.data)||
           (!has_depth&&r[0x0214/4])))
            throw std::logic_error("NV2A depth offset disagrees with guest target");
        push_constants_.surface_pending=0;
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
    // Only read valid, finite D3DMATERIAL8 / D3DLIGHT8 records. Keep
    // unknown lights distinct from disabled ones; never invent a default.
    bool ObserveMaterial(uint32_t p){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        if(!p||!mem_.IsCommitted(p,68))return false;
        std::array<float,17> value{};
        std::memcpy(value.data(),gpu::CheckedGuestRange(p,68),68);
        if(!std::all_of(value.begin(),value.end(),[](float f){return std::isfinite(f);})||value[16]<0)return false;
        state_.material=value;material_captured_=true;return true;
    }
    bool ObserveLight(uint32_t index,uint32_t p){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        if(index>=8||!p||!mem_.IsCommitted(p,104))return false;
        std::array<uint32_t,26> words{};
        std::memcpy(words.data(),gpu::CheckedGuestRange(p,104),104);
        if(words[0]<1||words[0]>3)return false;
        for(unsigned i=1;i<26;++i)if(!std::isfinite(std::bit_cast<float>(words[i])))return false;
        state_.lights[index]=words;light_defined_|=1u<<index;return true;
    }
    bool ObserveLightEnable(uint32_t index,uint32_t enabled){
        std::lock_guard guard(lock_);
        if(index>=8)return false;
        light_enable_seen_|=1u<<index;
        if(enabled)state_.light_enable|=1u<<index;else state_.light_enable&=~(1u<<index);
        return true;
    }
    std::pair<bool,uint32_t> LightingCoverage(){
        std::lock_guard guard(lock_);return {material_captured_,light_defined_};
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
    uint32_t SyncFixedDiffuseShader(uint32_t device,bool capture_pixel=false){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        gpu::CheckedGuestRange(device,0x24a0);
        auto ptr=Read(uint64_t(device)+0x794);
        if(!ptr||ptr%4)throw std::invalid_argument("Missing guest vertex shader object");
        gpu::CheckedGuestRange(ptr,0x14+16*16);
        const auto flags=Read(uint64_t(ptr)+4);
        auto pixel=Read(uint64_t(device)+0x784);
        const auto program_size=Read(uint64_t(ptr)+8),program_slots=Read(uint64_t(ptr)+12);
        const bool programmable=capture_pixel&&(flags&0x10u);
        if((pixel&&!capture_pixel)||(!programmable&&((flags!=0x400&&flags!=0)||program_size||program_slots))||
           (programmable&&((flags&~0x3c10u)||!program_size||program_size>136||program_slots<program_size)))
            throw std::logic_error("Unsupported guest shader program or flags");
        if(capture_pixel&&!programmable&&push_constants_.execution_mode_seen&&push_constants_.execution_mode==2)
            throw std::logic_error("NV2A program selected without supported vertex declaration");
        // Vulkan can translate multiple checked fixed or programmable
        // stages. Keep the GLES capture restrictions unchanged.
        std::array<uint32_t,4> textures{},palettes{};
        for(unsigned st=0;st<4;++st){
            textures[st]=Read(uint64_t(device)+0xf88+4*st);
            const auto palette=Read(uint64_t(device)+0xf98+4*st);
            if(!capture_pixel&&(palette||(st&&textures[st])))
                throw std::logic_error("Unsupported original texture stage or palette");
            if(!textures[st])continue;
            gpu::TextureDesc d{};
            if(!gpu::Describe(textures[st],d)||(!capture_pixel&&(d.cube||d.depth!=1))||
               Read(uint64_t(textures[st])+8)||!SupportedDiffuseTexture(d,capture_pixel))
                throw std::logic_error("Unsupported original texture resource");
            auto base=0x1b00+st*0x40;
            if(push_constants_.texture_words[st]!=3||
               Read(uint64_t(textures[st])+4)!=state_.simple[base/4]||
               Read(uint64_t(textures[st])+12)!=state_.simple[(base+4)/4])
                throw std::logic_error("Guest texture header disagrees with NV2A binding");
            if(d.linear){
                if((state_.simple[(base+0x10)/4]>>16)!=d.pitch||
                   state_.simple[(base+0x1c)/4]!=((d.width<<16)|d.height))
                    throw std::logic_error("Linear texture header disagrees with NV2A pitch or extent");
            }else if(d.format==0x0b){
                // X_D3DPalette: Common(type 0x30000, size bits 30..31),
                // Data, Lock. NV097_SET_TEXTURE_PALETTE: offset bits 6..31,
                // size bits 2..3, DMA select bit 0. Admit only 256 entries
                // in DMA A, the same physical memory domain as DMA texture 1.
                if(!palette||!push_constants_.palette_seen.test(st))
                    throw std::logic_error("Missing captured P8 palette");
                gpu::CheckedGuestRange(palette,12);
                const auto common=Read(palette),address=Read(uint64_t(palette)+4);
                const auto raw=state_.simple[(base+0x20)/4];
                const auto format=state_.simple[(base+4)/4],offset=state_.simple[base/4];
                if((common&0x70000u)!=0x30000u||(common>>30)||Read(uint64_t(palette)+8)||
                   address>=0x04000000u||address%64||raw!=address||
                   (format&3)!=1||(format&4)||((format>>4)&15)!=2||(format>>28)||
                   offset>=0x04000000u||offset%4||!((format>>16)&15))
                    throw std::logic_error("Unsupported or mismatched NV2A P8 palette binding");
                const auto bytes=gpu::TotalSize(d);
                if(!bytes||uint64_t(offset)+bytes>0x04000000u||uint64_t(address)+1024>0x04000000u)
                    throw std::out_of_range("P8 texture or palette exceeds physical RAM");
                gpu::CheckedGuestRange(d.data,bytes);
                gpu::CheckedGuestRange(gpu::PhysicalResourceAddress(address),1024);
                palettes[st]=palette;
            }else if(gpu::Format(d.format).depth){
                const auto format=state_.simple[(base+4)/4],offset=state_.simple[base/4];
                const auto bytes=uint64_t(d.width)*d.height*gpu::Format(d.format).bits/8;
                if((format&3)!=1||(format&4)||((format>>4)&15)!=2||(format>>28)||
                   offset%4||offset>=0x04000000u||((format>>16)&15)!=1||
                   !bytes||uint64_t(offset)+bytes>0x04000000u)
                    throw std::logic_error("Unsupported raw NV2A shadow texture layout");
                gpu::CheckedGuestRange(d.data,bytes);
            }else if(d.depth>1){
                const auto format=state_.simple[(base+4)/4],offset=state_.simple[base/4];
                const auto bytes=CheckedVolumeBytes(d);
                if((format&3)!=1||(format&4)||((format>>4)&15)!=3||offset%4||
                   offset>=0x04000000u||!((format>>16)&15)||uint64_t(offset)+bytes>0x04000000u)
                    throw std::logic_error("Unsupported raw NV2A volume texture layout");
                gpu::CheckedGuestRange(d.data,bytes);
            }else if(d.cube){
                // Cube faces use the upstream 128-byte face stride. Verify
                // DMA, square power-of-two dimensions and the whole span.
                const auto format=state_.simple[(base+4)/4],offset=state_.simple[base/4];
                const auto bytes=gpu::TotalSize(d);
                if((format&3)!=1||!(format&4)||((format>>4)&15)!=2||(format>>28)||
                   offset%4||offset>=0x04000000u||!((format>>16)&15)||
                   !bytes||uint64_t(offset)+bytes>0x04000000u)
                    throw std::logic_error("Unsupported raw NV2A cube texture layout");
                gpu::CheckedGuestRange(d.data,bytes);
            }else if(gpu::Format(d.format).swizzled){
                (void)DescribeNv2aSwizzledTexture(state_.simple[base/4],state_.simple[(base+4)/4]);
            }
        }
        uint32_t fvf=0;
        std::vector<uint32_t> candidates={0x42u,0x44u,0x142u,0x144u,0x242u,0x244u,0x342u,0x344u,0x442u,0x444u};
        // XDK FVF size codes: derive the candidate from each actual
        // declaration, then retain the existing exact slot/stride checks.
        if(capture_pixel&&flags==0x400){
            uint32_t sizes=0;
            for(unsigned st=0;st<4;++st){
                const auto type=Read(uint64_t(ptr)+0x14+16*(9+st)+8);
                const unsigned code=type==0x32?1:type==0x42?2:type==0x12?3:0;
                sizes|=code<<(16+2*st);
                if(sizes)for(uint32_t pos:{2u,4u})candidates.push_back(sizes|0x40u|pos|((st+1)<<8));
            }
        }
        if(capture_pixel&&flags==0x400){
            // Derive the FVF from the real register declaration rather than
            // assuming every mesh has a diffuse color and UV. The exact
            // per-slot and stride comparison below remains mandatory.
            auto type=[&](unsigned r){return Read(uint64_t(ptr)+0x14+16*r+8);};
            auto present=[&](unsigned r){return type(r)!=0&&type(r)!=2;};
            uint32_t f=type(0)==0x32?2u:type(0)==0x42?4u:0u;
            if(present(2))f|=0x10;
            if(present(3))f|=0x40;
            if(present(4))f|=0x80;
            unsigned count=0;
            for(unsigned st=0;st<4;++st)if(present(9+st)){
                count=st+1;
                auto t=type(9+st);
                f|=(t==0x32?1u:t==0x42?2u:t==0x12?3u:0u)<<(16+2*st);
            }
            f|=count<<8;
            if(SupportedVulkanFixedFvf(f))candidates.push_back(f);
        }
        for(uint32_t candidate:candidates){
            if(flags!=0x400||(!capture_pixel&&candidate>=0x200))continue;
            // A declaration can retain UVs after SetTexture(0,null).
            // Only a bound texture requires UV; keep the original stride.
            // FVF texture count is a four-bit field, not a flag:
            // TEX2 and TEX4 do not have bit 8 set.
            if(!capture_pixel&&std::any_of(textures.begin(),textures.end(),[](auto t){return t!=0;})&&!(candidate&0xf00u))continue;
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
        // RP6 APK after Start: XYZ + signed packed 11/11/10 normal +
        // UV, 24-byte stream. It is not an FVF with vertex diffuse color.
        // Preserve the exact declaration for the upstream geometry assembler.
        // The GLES lighting stage is still deliberately guarded.
        std::shared_ptr<gpu::VertexShader> packed;
        if(!programmable&&!fvf&&flags==0&&(textures[0]||capture_pixel)&&state_.streams[0].vb&&state_.streams[0].stride==24){
            auto layout=std::make_shared<gpu::VertexShader>();
            layout->inputs.fill({});
            layout->inputs[0]={0,0,0x32};
            layout->inputs[2]={0,12,0x16};
            layout->inputs[9]={0,16,0x22};
            layout->stream_stride[0]=24;
            bool match=true;
            for(unsigned i=0;i<16;++i){
                auto slot=uint64_t(ptr)+0x14+16*i;
                const auto& want=layout->inputs[i];
                auto type=Read(slot+8);
                if(want.stream<0){
                    if(type!=0&&type!=2)match=false;
                }else if(type!=want.type||Read(slot)!=uint32_t(want.stream)||
                         Read(slot+4)!=want.offset||Read(slot+12))match=false;
            }
            if(match){packed=std::move(layout);fvf=ptr|1u;}
        }
        // The upstream fixed shader reads conventional position, normal,
        // diffuse/specular and texture registers from arbitrary declarations.
        // Only use its checked XYZ subset, not blend weights, point size,
        // tessellation, implicit attributes or pretransformed custom vertices.
        // XDK FVF-tagged declarations can retain a packed normal rather than
        // the float3 normal implied by the canonical FVF. Preserve the actual
        // slots instead of forcing an incompatible canonical layout.
        const bool packed_fvf=flags==0x400&&Read(uint64_t(ptr)+0x14+16*2+8)==0x16;
        if(capture_pixel&&!programmable&&!fvf&&(flags==0||packed_fvf)){
            auto layout=std::make_shared<gpu::VertexShader>();
            layout->handle=ptr|1u;
            for(unsigned i=0;i<16;++i){
                auto slot=uint64_t(ptr)+0x14+16*i;
                auto stream=Read(slot),offset=Read(slot+4),type=Read(slot+8);
                if(!type||type==2)continue;
                unsigned bytes=0;
                if(i==0&&type==0x32)bytes=12;
                else if(i==2&&(type==0x32||type==0x16))bytes=type==0x32?12:4;
                else if((i==3||i==4)&&type==0x40)bytes=4;
                else if(i>=9&&i<=12&&(type==0x12||type==0x22||type==0x32||type==0x42))bytes=(type>>4)*4;
                if(!bytes||stream>=16||!state_.streams[stream].vb||Read(slot+12)||
                   uint64_t(offset)+bytes>state_.streams[stream].stride)
                    throw std::logic_error("Unsupported fixed Xbox vertex declaration input");
                layout->inputs[i]={int(stream),offset,type};
                layout->stream_stride[stream]=state_.streams[stream].stride;
            }
            if(layout->inputs[0].stream<0)throw std::logic_error("Fixed Xbox declaration lacks XYZ position");
            packed=std::move(layout);fvf=ptr|1u;
        }
        if(programmable){
            auto layout=std::make_shared<gpu::VertexShader>();
            layout->handle=ptr|1u;layout->program_type=0x2078;
            layout->program=CaptureNv2aVertexProgram(push_constants_,program_size);
            for(unsigned i=0;i<16;++i){
                auto slot=uint64_t(ptr)+0x14+16*i;
                auto stream=Read(slot),offset=Read(slot+4),type=Read(slot+8);
                if(!type||type==2)continue;
                // Explicit supported formats only; no tessellation or implicit
                // vertex inputs. Bounds are checked again by the assembler.
                unsigned bytes=0;
                switch(type){
                    case 0x12:case 0x16:case 0x40:bytes=4;break;
                    case 0x22:bytes=8;break;
                    case 0x32:case 0x72:bytes=12;break;
                    case 0x42:bytes=16;break;
                    case 0x11:case 0x15:bytes=2;break;
                    case 0x21:case 0x25:bytes=4;break;
                    case 0x31:case 0x35:bytes=6;break;
                    case 0x41:case 0x45:bytes=8;break;
                    case 0x14:bytes=1;break;case 0x24:bytes=2;break;
                    case 0x34:bytes=3;break;case 0x44:bytes=4;break;
                    default:throw std::logic_error("Unsupported NV2A program vertex format");
                }
                if(stream>=16||!state_.streams[stream].vb||Read(slot+12)&0xffffu||
                   uint64_t(offset)+bytes>state_.streams[stream].stride)
                    throw std::logic_error("Unsupported NV2A program vertex input");
                layout->inputs[i]={int(stream),offset,type};
                layout->stream_stride[stream]=state_.streams[stream].stride;
            }
            if(layout->inputs[0].stream<0)throw std::logic_error("NV2A program has no position input");
            packed=std::move(layout);fvf=ptr|1u;
        }
        if(!fvf)throw std::logic_error("Guest fixed vertex declaration not equivalent to supported diffuse or packed-normal layout");
        auto next=state_;
        next.vs_handle=fvf;next.vs=std::move(packed);next.ps.reset();
        if(pixel){
            gpu::CheckedGuestRange(pixel,4);
            next.ps=CaptureNv2aCombiner(next,push_constants_,pixel);
            next.rs[gpu::kRsPsTextureModes]=next.ps->def[54];
            for(unsigned st=0;st<4;++st){
                auto mode=(next.ps->def[54]>>(5*st))&31;
                if((mode==1||mode==2||mode==3||mode==6||mode==7||mode==0xf||mode==0x10)!=bool(textures[st]))
                    throw std::logic_error("Captured texture binding disagrees with pixel stage mode");
                if(textures[st]){
                    gpu::TextureDesc d{};
                    if(!gpu::Describe(textures[st],d)||d.cube!=(mode==3)||((d.depth>1||gpu::Format(d.format).depth)!=(mode==2)))
                        throw std::logic_error("Captured texture dimension disagrees with pixel stage mode");
                }
                if(mode==6||mode==7){
                    const uint8_t required=mode==7?0x3f:0x0f;
                    if((push_constants_.bump_seen[st]&required)!=required)
                        throw std::logic_error("Incomplete captured NV2A bump constants");
                    constexpr unsigned fields[]={gpu::kTssBumpEnvMat00,gpu::kTssBumpEnvMat01,
                        gpu::kTssBumpEnvMat11,gpu::kTssBumpEnvMat10,gpu::kTssBumpEnvLScale,gpu::kTssBumpEnvLOffset};
                    for(unsigned k=0;k<(mode==7?6u:4u);++k)
                        next.tss[st][fields[k]]=state_.simple[(0x1b28+st*0x40+4*k)/4];
                }
            }
        }
        next.textures=textures;next.palettes=palettes;
        next.base_vertex=Read(uint64_t(device)+0x1c);
        // Copy the actual matrices instead of using the standalone identity.
        // XDK device matrix mirror: view/projection, texture 0..3,
        // world. Match pinned SCDA D3DDevice_SetTransform.
        for(unsigned tr=0;tr<=6;++tr){
            if(!capture_pixel&&tr>=2&&tr<=5)continue;
            auto pc=tr<2?tr+2:tr<6?tr+14:tr+250;
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
        state_=std::move(next);ViewportConstants(capture_pixel);return fvf;
    }
    // Prove that the real stream can be assembled, even if later raster
    // translation must stop. No backend draw or guest return is implied.
    uint32_t VerifyGuestGeometry(uint32_t prim,uint32_t start,uint32_t count,uint32_t indices,gpu::Assembled* prepared=nullptr){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        if(!count||count>262144||prim<1||prim>10)
            throw std::invalid_argument("Invalid guest geometry request");
        auto* ip=indices?gpu::CheckedGuestRange(indices,uint64_t(count)*2):nullptr;
        gpu::Assembled local;
        auto& a=prepared?*prepared:local;
        uint32_t regs=0;
        gpu::VertexShader fvf_layout;
        if(!state_.vs)gpu::FvfInputs(state_.vs_handle,fvf_layout);
        const auto& layout=state_.vs?*state_.vs:fvf_layout;
        for(unsigned r=0;r<16;++r)if(layout.inputs[r].stream>=0)regs|=1u<<r;
        const bool program=state_.vs&&!state_.vs->program.empty();
        if(!gpu::Assemble(state_,prim,start,count,ip,a)||(program?!(a.regs&1):a.regs!=regs))
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
        if(!defer_draw_flush_)backend_.Flush();
    }
    // First original textured quad: use the checked real declaration,
    // resource, matrices, viewport and blend/alpha/depth parameters.
    // This is an offscreen diagnostic replay, not general NV2A draw
    // translation: culling, multisampling and raw clip state are pending.
    // Preserve all observed raster and sampler words when extending the
    // reduced replay. Resource addresses and dimensions may vary; each
    // resource is independently validated by SyncFixedDiffuseShader.
    static bool SameDiagnosticTextureProfile(const gpu::State& a,const gpu::State& b){
        if(a.rs!=b.rs||a.tss!=b.tss||a.vs_handle!=b.vs_handle||a.vs||b.vs||a.ps||b.ps||
           !a.textures[0]||!b.textures[0]||a.palettes!=b.palettes||
           a.rt.header!=b.rt.header||a.zs.header!=b.zs.header||
           a.base_vertex!=b.base_vertex||a.scissor_count!=b.scissor_count||
           a.scissors!=b.scissors||a.viewport.x!=b.viewport.x||a.viewport.y!=b.viewport.y||
           a.viewport.w!=b.viewport.w||a.viewport.h!=b.viewport.h||
           a.viewport.min_z!=b.viewport.min_z||a.viewport.max_z!=b.viewport.max_z||
           a.ss_offset_x!=b.ss_offset_x||a.ss_offset_y!=b.ss_offset_y)return false;
        for(unsigned i=1;i<4;++i)if(a.textures[i]||b.textures[i])return false;
        for(size_t i=0;i<a.simple.size();++i){
            if(i==(0x1b00>>2)||i==(0x1b04>>2))continue;
            if(a.simple[i]!=b.simple[i])return false;
        }
        return true;
    }
    static bool SupportedDiffuseTexture(const gpu::TextureDesc& d,bool allow_palette=false){
        const auto& f=gpu::Format(d.format);
        // Color formats already decoded by the pinned XboxXBERecomp code.
        // P8 is admitted only by the checked Vulkan live palette path.
        // Depth and signed bump-map data still require separate translation.
        switch(d.format){
            case 0x0b:if(!allow_palette)return false;break;
            case 0x2a:case 0x2c:case 0x2e:case 0x30:
                if(!allow_palette||d.cube||d.depth!=1||d.levels!=1)return false;break;
            case 0x00:case 0x01:case 0x02:case 0x03:case 0x04:case 0x05:
            case 0x06:case 0x07:case 0x0c:case 0x0e:case 0x0f:
            case 0x10:case 0x11:case 0x12:case 0x13:case 0x19:case 0x1a:
            case 0x1b:case 0x1c:case 0x1d:case 0x1e:case 0x1f:case 0x20:
            case 0x24:case 0x25:case 0x32:case 0x35:
            case 0x38:case 0x39:case 0x3a:case 0x3b:case 0x3c:
            case 0x3d:case 0x3e:case 0x3f:case 0x40:case 0x41:break;
            default:return false;
        }
        unsigned max_levels=1;
        for(unsigned extent=std::max({d.width,d.height,d.depth});extent>1;extent>>=1)++max_levels;
        return d.width&&d.height&&d.width<=4096&&d.height<=4096&&d.levels>=1&&
            d.levels<=max_levels&&(!d.linear||d.levels==1)&&
            (d.depth==1||(allow_palette&&d.depth>1&&!d.linear&&!d.cube&&f.swizzled&&!f.compressed&&f.host==gpu::Host::BGRA8&&d.format!=0x0b))&&(!d.cube||(allow_palette&&!d.linear&&d.width==d.height&&d.depth==1&&d.format!=0x0b))&&d.linear==(!f.swizzled&&!f.compressed)&&
            (!d.linear||d.pitch>=uint64_t(d.width)*f.bits/8)&&
            ((d.format!=0x24&&d.format!=0x25)||!(d.width&1));
    }
    void ReplayTexturedQuadDiagnostic(uint32_t start,uint32_t count){
        ReplayTexturedDiffuseDiagnostic(6,start,count);
    }
    void ReplayTexturedDiffuseDiagnostic(uint32_t prim,uint32_t start,uint32_t count,uint32_t indices=0){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        const bool packed=bool(state_.vs);
        if(packed&&(!material_captured_||!light_enable_seen_||(state_.light_enable&~light_defined_)))
            throw std::logic_error("Packed normal lighting requires captured material and enabled light records");
        if(!initialized_||(!packed&&state_.vs_handle!=0x142)||state_.ps||
           (packed&&!state_.textures[0])||!count||count>262144||
           !((prim==5&&count%3==0)||(prim==6&&count>=3))||state_.scissor_count||
           state_.viewport.x||state_.viewport.y||state_.viewport.min_z!=0||
           state_.viewport.max_z!=1||state_.ss_offset_x||state_.ss_offset_y||
           (packed?state_.rs[gpu::kRsLighting]!=1:state_.rs[gpu::kRsLighting]!=0)||state_.rs[gpu::kRsFogEnable]||
           (!packed&&state_.rs[gpu::kRsSpecularEnable])||state_.rs[gpu::kRsVertexBlend]||
           state_.rs[gpu::kRsStencilEnable]||(packed&&state_.rs[gpu::kRsTwoSidedLighting])||
           std::any_of(state_.textures.begin()+1,state_.textures.end(),[](auto p){return p!=0;})||
           std::any_of(state_.palettes.begin(),state_.palettes.end(),[](auto p){return p!=0;}))
            throw std::logic_error("Unsupported original textured quad profile");
        gpu::TextureDesc desc{},tex{};
        if(!gpu::DescribeSurface(state_.rt,desc)||state_.viewport.w!=desc.width||state_.viewport.h!=desc.height||
           (state_.textures[0]&&(!gpu::Describe(state_.textures[0],tex)||!SupportedDiffuseTexture(tex))))
            throw std::logic_error("Textured diffuse draw requires full target and supported color texture");
        const auto& st=state_.tss[0];
        // RP6 stage 0: clamp, bilinear, modulate diffuse * texture for
        // both RGB and alpha. Arg0 is unused by a two-input modulate.
        auto address_ok=[](uint32_t mode){return mode>=1&&mode<=3;};
        const bool textured=state_.textures[0]!=0;
        if(textured&&(!address_ok(st[gpu::kTssAddressU])||!address_ok(st[gpu::kTssAddressV])||
           !address_ok(st[gpu::kTssAddressW])||
           (st[gpu::kTssMinFilter]!=1&&st[gpu::kTssMinFilter]!=2)||
           (st[gpu::kTssMagFilter]!=1&&st[gpu::kTssMagFilter]!=2)||st[gpu::kTssMipFilter]>2||
           st[gpu::kTssMipLodBias]||st[gpu::kTssMaxMipLevel]||
           st[gpu::kTssMaxAnisotropy]!=1||st[gpu::kTssColorKeyOp]||
           st[gpu::kTssColorSign]||st[gpu::kTssAlphaKill]||
           st[gpu::kTssColorOp]!=4||st[gpu::kTssColorArg1]!=0||st[gpu::kTssColorArg2]!=2||
           st[gpu::kTssAlphaOp]!=4||st[gpu::kTssAlphaArg1]!=0||st[gpu::kTssAlphaArg2]!=2||
           st[gpu::kTssResultArg]!=1||st[gpu::kTssTextureTransformFlags]||st[gpu::kTssTexCoordIndex]))
            throw std::logic_error("Unsupported original quad texture combiner");
        if(!textured&&(st[gpu::kTssColorOp]!=1||st[gpu::kTssAlphaOp]!=1))
            throw std::logic_error("Unbound texture requires disabled color and alpha stages");
        for(unsigned i=1;i<4;++i)
            if(state_.tss[i][gpu::kTssColorOp]>1||state_.tss[i][gpu::kTssAlphaOp]>1)
                throw std::logic_error("Additional active texture stage");
        auto replay=state_;
        replay.rs.fill(0);replay.tss={};replay.simple.fill(0);
        replay.rs[gpu::kRsZEnable]=state_.rs[gpu::kRsZEnable];
        if(packed){
            for(auto i:{gpu::kRsLighting,gpu::kRsSpecularEnable,gpu::kRsLocalViewer,gpu::kRsColorVertex,
                        gpu::kRsNormalizeNormals,gpu::kRsDiffuseMaterialSource,gpu::kRsSpecularMaterialSource,
                        gpu::kRsAmbientMaterialSource,gpu::kRsEmissiveMaterialSource,gpu::kRsAmbient,
                        gpu::kRsFrontFace,gpu::kRsCullMode,gpu::kRsFillMode,gpu::kRsBackFillMode})
                replay.rs[i]=state_.rs[i];
        }
        for(auto m:{gpu::kMAlphaTestEnable,gpu::kMAlphaFunc,gpu::kMAlphaRef,
                    gpu::kMBlendEnable,gpu::kMSrcBlend,gpu::kMDestBlend,gpu::kMBlendOp,
                    gpu::kMZFunc,gpu::kMZWriteEnable,gpu::kMColorWriteEnable,
                    gpu::kMDitherEnable,gpu::kMShadeMode})
            replay.simple[m>>2]=state_.simple[m>>2];
        auto& dst=replay.tss[0];
        if(textured){
            for(auto k:{gpu::kTssAddressU,gpu::kTssAddressV,gpu::kTssMinFilter,gpu::kTssMagFilter,
                        gpu::kTssMipFilter,gpu::kTssColorOp,gpu::kTssAlphaOp})dst[k]=st[k];
            // Multiplication commutes; normalize argument order.
            dst[gpu::kTssColorArg1]=dst[gpu::kTssAlphaArg1]=2;
        }
        auto* ip=indices?gpu::CheckedGuestRange(indices,uint64_t(count)*2):nullptr;
        backend_.Draw(replay,prim,start,count,ip);if(!defer_draw_flush_)backend_.Flush();
    }
    // The upstream assembler converts fans, quad lists/strips and convex
    // polygons to triangle indices. Reject incomplete primitives rather
    // than letting a malformed draw be silently truncated.
    static bool CapturedPrimitive(uint32_t prim,uint32_t count){
        switch(prim){
        case 2:return count>=2&&count%2==0;
        case 3:return count>=3;
        case 4:return count>=2;
        case 5:return count>=3&&count%3==0;
        case 6:case 7:case 10:return count>=3;
        case 8:return count>=4&&count%4==0;
        case 9:return count>=4&&count%2==0;
        default:return false; // point-size output not yet translated
        }
    }
    // Validate fixed-function combiner operations actually implemented by
    // PixelHlsl, avoiding its fallback for unknown operations/arguments.
    static void ValidateFixedStages(const gpu::State& s){
        using namespace gpu;
        auto check_op=[](uint32_t op,uint32_t a0,uint32_t a1,uint32_t a2,bool bound,bool alpha){
            if(op<=1)return;
            // Shared HLSL handles these operations without silent fallbacks.
            if(op>24||op==17||(alpha&&op>=18&&op<=22))
                throw std::logic_error("Unsupported fixed texture operation");
            auto arg=[&](uint32_t v){
                if((v&~0x3fu)||(v&15)>5)throw std::logic_error("Invalid fixed texture argument");
                if((v&15)==2&&!bound)throw std::logic_error("Fixed texture argument lacks resource");
            };
            if(op!=3)arg(a1);
            if(op!=2)arg(a2);
            if(op==23||op==24)arg(a0);
            if((op==14||op==16)&&!bound)throw std::logic_error("Fixed texture operation lacks resource");
        };
        for(unsigned i=0;i<4;++i){
            const auto& t=s.tss[i];const bool bound=s.textures[i]!=0;
            const auto color=t[kTssColorOp],alpha=t[kTssAlphaOp];
            // D3DTOP_DISABLE terminates the fixed-function cascade.
            // Higher stages can retain old state; it is not executed.
            if(color<=1)break;
            if(t[kTssResultArg]!=1&&t[kTssResultArg]!=5)
                throw std::logic_error("Unsupported fixed texture result");
            check_op(color,t[kTssColorArg0],t[kTssColorArg1],t[kTssColorArg2],bound,false);
            check_op(alpha,t[kTssAlphaArg0],t[kTssAlphaArg1],t[kTssAlphaArg2],bound,true);
            if(t[kTssColorKeyOp]||t[kTssColorSign]||
               (t[kTssAlphaKill]&&t[kTssAlphaKill]!=4))
                throw std::logic_error("Unsupported fixed texture sampling state");
        }
    }
    // Validated captured shader path; fixed-function extension is explicitly
    // selected by the live Vulkan hook, never the legacy GLES caller.
    // prepared is optional and may only be supplied by the same
    // serialized live draw immediately after VerifyGuestGeometry.
    void ReplayCapturedShaderDiagnostic(uint32_t prim,uint32_t start,uint32_t count,uint32_t indices=0,bool allow_fixed=false,const gpu::Assembled* prepared=nullptr){
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        const bool program=state_.vs&&!state_.vs->program.empty();
        const auto fvf=state_.vs_handle;
        const auto base_fvf=fvf&0xffffu;
        const bool rhw=!state_.vs&&(base_fvf==0x44||base_fvf==0x144)&&(!(fvf>>16)||allow_fixed);
        const bool multi_uv=allow_fixed&&!state_.vs&&
            (base_fvf==0x142||base_fvf==0x144||base_fvf==0x242||base_fvf==0x244||
             base_fvf==0x342||base_fvf==0x344||base_fvf==0x442||base_fvf==0x444);
        const bool fixed=allow_fixed&&!state_.ps&&!program;
        const bool packed=allow_fixed&&state_.vs&&!program;
        const bool general_fvf=allow_fixed&&!state_.vs&&SupportedVulkanFixedFvf(fvf);
        const bool normal=packed?state_.vs->inputs[2].stream>=0:general_fvf&&(fvf&0x10);
        const bool lighting=state_.rs[gpu::kRsLighting]!=0;
        if(!initialized_||(!state_.ps&&!program&&!fixed)||
           (!program&&!packed&&!general_fvf&&(state_.vs||(state_.vs_handle!=0x142&&state_.vs_handle!=0x42&&!rhw&&!multi_uv)))||
           count>262144||!CapturedPrimitive(prim,count)||
           state_.scissor_count>1||(!program&&(state_.ss_offset_x||state_.ss_offset_y))||
           (lighting&&(!fixed||!normal||!material_captured_||!light_enable_seen_||(state_.light_enable&~light_defined_)))||
           state_.rs[gpu::kRsFogEnable]>1||(fixed&&state_.rs[gpu::kRsRangeFogEnable])||
           // Lit fixed draws may use table fog (RP6 first-phase draw, mode 3);
           // range fog stays unsupported via the clause above.
           state_.rs[gpu::kRsVertexBlend]||state_.rs[gpu::kRsTwoSidedLighting])
            throw std::logic_error("Unsupported captured shader replay profile");
        gpu::TextureDesc desc{};
        if(!gpu::DescribeSurface(state_.rt,desc)||!state_.viewport.w||!state_.viewport.h||
           uint64_t(state_.viewport.x)+state_.viewport.w>desc.width||uint64_t(state_.viewport.y)+state_.viewport.h>desc.height)
            throw std::logic_error("Captured shader viewport outside target");
        if(fixed)ValidateFixedStages(state_);
        auto replay=state_;replay.rs.fill(0);replay.simple.fill(0);
        if(fixed){
            // Normalize only the draw-local copy. Do not erase latent
            // guest stage state, which can become active on a later draw.
            bool ended=false;
            for(unsigned st=0;st<4;++st){
                ended=ended||replay.tss[st][gpu::kTssColorOp]<=1;
                if(!ended)continue;
                replay.tss[st].fill(0);
                replay.tss[st][gpu::kTssColorOp]=replay.tss[st][gpu::kTssAlphaOp]=1;
                replay.textures[st]=0;replay.palettes[st]=0;
            }
        }
        bool current_v11=false;
        if(program&&push_constants_.current_v11_seen&&state_.vs->inputs[11].stream<0){
            // Operand mux 2 = vertex input; shared input selector word1[12:9].
            const auto& words=state_.vs->program;
            for(size_t n=0;n+4<=words.size();n+=4)
                if(((words[n+1]>>9)&15)==11&&
                   (((words[n+2]>>26)&3)==2||((words[n+2]>>11)&3)==2||((words[n+3]>>28)&3)==2))
                    current_v11=true;
            if(current_v11&&!allow_fixed)throw std::logic_error("Current NV2A v11 requires Vulkan replay");
        }
        if(!program){
            gpu::VertexShader fvf_layout;
            if(!state_.vs)gpu::FvfInputs(state_.vs_handle,fvf_layout);
            const auto& layout=state_.vs?*state_.vs:fvf_layout;
            for(unsigned st=0;st<4;++st){
                const auto& raw=push_constants_.texgen[st];
                const bool generated=std::any_of(raw.begin(),raw.end(),[](auto v){return v!=0;});
                const auto mode=(state_.rs[gpu::kRsPsTextureModes]>>(5*st))&31;
                if(!replay.textures[st]&&!(state_.ps&&generated&&(mode==4||mode==5)))continue;
                auto& t=replay.tss[st];const auto set=t[gpu::kTssTexCoordIndex];
                const auto flags=t[gpu::kTssTextureTransformFlags],components=flags&0xff;
                // Shared shader supports row-vector texture matrices and
                // projected 2D coordinates with a third/fourth q component.
                if(flags&&(!allow_fixed||rhw||(flags&~0x1ffu)||
                   components<2||components>4||((flags&0x100)&&components<3)))
                    throw std::logic_error("Unsupported captured texture transform profile");
                if(generated){
                    // Shared FixedVertexHlsl already implements camera-space
                    // reflection vectors. Require the full S/T/R profile,
                    // position/normal inputs and a supported texture transform.
                    if(!allow_fixed||rhw||raw[0]!=0x8512||raw[1]!=0x8512||
                       raw[2]!=0x8512||raw[3]!=0||layout.inputs[2].stream<0)
                        throw std::logic_error("Unsupported active NV2A reflection coordinate profile");
                    t[gpu::kTssTexCoordIndex]=(set&3u)|0x30000u;
                    continue;
                }
                if(set>=4||(!allow_fixed&&set)||layout.inputs[9+set].stream<0)
                    throw std::logic_error("Captured fixed vertex path requires declared UV set");
                gpu::TextureDesc d{};
                if(replay.textures[st]&&gpu::Describe(replay.textures[st],d)&&(d.cube||d.depth>1||gpu::Format(d.format).depth)){
                    const auto type=layout.inputs[9+set].type;
                    if((!flags&&type!=0x32&&type!=0x42)||(flags&&components<3)||(!state_.ps&&(flags&0x100)&&!gpu::Format(d.format).depth))
                        throw std::logic_error("Unsupported cube or volume coordinate layout");
                }
            }
        }
        for(auto r:{gpu::kRsZEnable,gpu::kRsPsTextureModes,gpu::kRsSpecularEnable,
                    gpu::kRsStencilEnable,gpu::kRsStencilFail,gpu::kRsCullMode,gpu::kRsFillMode,gpu::kRsBackFillMode,
                    gpu::kRsFogEnable,gpu::kRsFogTableMode,gpu::kRsFogStart,gpu::kRsFogEnd,gpu::kRsFogDensity,gpu::kRsFogColor,
                    gpu::kRsTextureFactor,gpu::kRsShadowFunc})
            replay.rs[r]=state_.rs[r];
        if(allow_fixed&&push_constants_.shadow_compare_seen){
            // NV097 raw order (nxdk) is the reversed D3DCMP ordering.
            constexpr uint32_t swap[]={0,4,2,6,1,5,3,7};
            replay.rs[gpu::kRsShadowFunc]=0x200+swap[push_constants_.shadow_compare];
        }
        if(fixed&&lighting)for(auto r:{gpu::kRsLighting,gpu::kRsLocalViewer,gpu::kRsColorVertex,
            gpu::kRsNormalizeNormals,gpu::kRsDiffuseMaterialSource,gpu::kRsSpecularMaterialSource,
            gpu::kRsAmbientMaterialSource,gpu::kRsEmissiveMaterialSource,gpu::kRsAmbient})
            replay.rs[r]=state_.rs[r];
        for(auto m:{gpu::kMAlphaTestEnable,gpu::kMAlphaFunc,gpu::kMAlphaRef,gpu::kMBlendEnable,gpu::kMSrcBlend,
                    gpu::kMDestBlend,gpu::kMBlendOp,gpu::kMBlendColor,gpu::kMZFunc,gpu::kMZWriteEnable,gpu::kMColorWriteEnable,
                    gpu::kMShadeMode,gpu::kMStencilWriteMask,gpu::kMStencilFunc,gpu::kMStencilRef,
                    gpu::kMStencilMask,gpu::kMStencilZFail,gpu::kMStencilPass,
                    gpu::kMSolidOffsetEnable,gpu::kMSlopeScale,gpu::kMDepthOffset,gpu::kMCountEnable})
            replay.simple[m/4]=state_.simple[m/4];
        for(uint32_t m=0;m<0x2000;m+=4)if(IsCombinerMethod(m))replay.simple[m/4]=state_.simple[m/4];
        auto* ip=indices?gpu::CheckedGuestRange(indices,uint64_t(count)*2):nullptr;
        if(current_v11){
            gpu::Assembled local;
            const auto* source=prepared;
            if(!source){
                if(!gpu::Assemble(replay,prim,start,count,ip,local))
                    throw std::logic_error("Cannot assemble latched NV2A vertex input");
                source=&local;
            }
            if(!source->vertex_count||source->vertex_count>262144||
               source->buffer_count>=source->buffers.size()||(source->regs&(1u<<11)))
                throw std::logic_error("Invalid current NV2A vertex input assembly");
            // XboxXBERecomp ApplyImmediate decodes SET_VERTEX_DATA4UB as
            // low byte X, then Y,Z,W normalized by 255. R8G8B8A8_UNORM
            // provides exactly that ordering, unlike D3DCOLOR BGRA.
            // Repeat for every assembled vertex (including dense indexed
            // spans); original indices and source buffers stay untouched.
            std::vector<uint32_t> values(source->vertex_count,push_constants_.current_v11);
            auto expanded=*source; // source owns original pointed-to storage
            const auto slot=expanded.buffer_count++;
            expanded.buffers[slot]={reinterpret_cast<const uint8_t*>(values.data()),uint32_t(values.size()*4),4};
            expanded.elements[11]={gpu::kVertexR8G8B8A8_UNORM,uint8_t(slot),0};
            expanded.regs|=1u<<11;
            expanded.decode&=~(uint64_t(15)<<(11*4));
            backend_.DrawAssembled(replay,prim,start,count,ip,expanded);
        }else if(prepared)backend_.DrawAssembled(replay,prim,start,count,ip,*prepared);
        else backend_.Draw(replay,prim,start,count,ip);
        if(!defer_draw_flush_)backend_.Flush();
    }
    void ApplyPush(uint32_t begin,uint32_t end,bool capture_pixel=false,bool compact=false){
        std::lock_guard guard(lock_);
        if(!initialized_)throw std::logic_error("D3D bridge not initialized");
        // Validate and decode newly complete raw texture bindings before
        // committing the scratch batch. Do not enable a draw just because
        // the image data decodes; sampler, shader and raster state still matter.
        auto methods=DecodeNv2aMethods(mem_,begin,end);
        auto next=state_;auto cursor=push_constants_;
        gpu::ScopedGuestMemory scope(mem_);
        std::vector<Nv2aMethod> reports;
        if(capture_pixel){
            // State-only packets cannot contain draws. Validate the entire
            // batch before performing ordered visibility clear/report actions.
            std::vector<Nv2aMethod> state_methods;
            for(auto method:methods){
                auto [m,v]=method;
                if(m==gpu::kMClearReport){
                    if(v!=1)throw std::logic_error("Unsupported NV2A clear report type");
                    reports.push_back(method);
                }else if(m==gpu::kMGetReport){
                    if((v>>24)!=1||(v&15))throw std::logic_error("Unsupported NV2A visibility report type or alignment");
                    gpu::CheckedGuestRange(gpu::PhysicalResourceAddress(v&0xffffffu),16);
                    reports.push_back(method);
                }else state_methods.push_back(method);
            }
            ApplyNv2aState(next,cursor,state_methods,true);
        }else ApplyNv2aState(next,cursor,methods,false);
        for(unsigned stage=0;stage<4;++stage){
            if(!cursor.TextureReady(stage))continue;
            const auto base=0x1b00+stage*0x40;
            auto offset=next.simple[base>>2],format=next.simple[(base+4)>>2];
            // Only decode the verified swizzled 2D family here; preserve
            // all other bindings for explicit validation at the draw boundary.
            if((format&3)==1&&!(format&4)&&((format>>4)&15)==2&&
               gpu::Format((format>>8)&255).swizzled&&
               gpu::Format((format>>8)&255).host==gpu::Host::BGRA8&&((format>>8)&255)!=0xb){
                if(compact&&capture_pixel){
                    // Keep address/layout validation; actual decoding happens
                    // in the Vulkan texture cache when the resource is sampled.
                    (void)DescribeNv2aSwizzledTexture(offset,format);
                }else{
                    auto probe=ProbeNv2aSwizzledTexture(offset,format);
                    std::fprintf(stderr,"D3D RAW TEXTURE DECODE PASS stage=%u physical=%08X format=%08X %ux%u mips=%u hash=%016llX (not sampled)\n",
                        stage,offset,format,probe.desc.width,probe.desc.height,probe.desc.levels,
                        static_cast<unsigned long long>(probe.hash));
                }
            }else if(!compact||!capture_pixel){
                std::fprintf(stderr,"D3D RAW TEXTURE PENDING stage=%u physical=%08X format=%08X\n",stage,offset,format);
            }
            cursor.TextureInspected(stage);
        }
        for(auto [m,v]:reports){
            if(m==gpu::kMClearReport)backend_.ClearReport();
            else backend_.GetReport(gpu::PhysicalResourceAddress(v&0xffffffu));
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
