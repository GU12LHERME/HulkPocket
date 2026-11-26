#pragma once
// Portable subset of XboxXBERecomp's D3D8 state transitions.
// This is the graphics-side bridge, not yet a guest hook replacement:
// device mirrors, reference ownership and pushbuffer walking belong to the next layer.
#include "gpu/backend.h"
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
                if(!v.w||!v.h||v.w>4096||v.h>4096||!std::isfinite(v.min_z)||!std::isfinite(v.max_z))
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
    // Explicit present endpoint, called only by the owning device layer.
    uint32_t Present(uint32_t surface) {
        std::lock_guard guard(lock_);gpu::ScopedGuestMemory scope(mem_);
        if(!initialized_||!surface)throw std::logic_error("No initialized present surface");
        backend_.Present(Surface(surface));return ++presents_;
    }
    gpu::State Snapshot(){std::lock_guard guard(lock_);return state_;}
};
}
