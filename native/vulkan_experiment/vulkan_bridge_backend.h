#pragma once
#include "vulkan_core.h"
#include "gpu/backend.h"
#include "gpu/guest_memory.h"
namespace hulkpocket::vkexp {
// First real Backend ABI integration. Synchronous, offscreen and strict:
// unsupported guest commands fail rather than silently corrupting frames.
class VulkanBridgeBackend final:public gpu::Backend {
    VulkanCore core_;
    gpu::Surface target_{};
    uint32_t address_=0,pitch_=0;
    void Target(const gpu::Surface& s){
        gpu::TextureDesc d{};
        if(!gpu::DescribeSurface(s,d)||!d.linear||d.format!=0x12||d.width!=core_.W||d.height!=core_.H)
            throw std::invalid_argument("Unsupported Vulkan bridge target");
        gpu::CheckedGuestRange(d.data,uint64_t(d.pitch)*d.height);
        if(target_.header&&(target_.header!=s.header||address_!=d.data))
            throw std::logic_error("Vulkan target switching/rotation not integrated");
        target_=s;address_=d.data;pitch_=d.pitch;
    }
    void Writeback(){
        if(!target_.header||!core_.frame_valid)throw std::logic_error("No initialized Vulkan frame");
        StoreColor(target_,static_cast<const uint8_t*>(core_.readback.data),core_.W,core_.H);
    }
public:
    bool Init(uint32_t w,uint32_t h)override{core_.Init(w,h);return true;}
    void Clear(const gpu::State& s,uint32_t count,const uint8_t*,uint32_t flags,uint32_t color,float z,uint32_t stencil)override{
        if(count)throw std::logic_error("Vulkan rectangle clear not integrated");
        Target(s.rt);
        // An absent guest depth surface still has an internal attachment.
        // Initialize it explicitly at the first color clear only.
        if(!core_.frame_valid&&!s.zs.header&&flags==0xf0){flags|=3;z=1;stencil=0;}
        core_.ClearFrame(flags,color,z,stencil);
    }
    void Draw(const gpu::State& s,uint32_t prim,uint32_t start,uint32_t count,const uint8_t* indices)override{
        Target(s.rt);
        if(!core_.frame_valid)throw std::logic_error("Vulkan draw before clear");
        if(s.viewport.x||s.viewport.y||s.viewport.w!=core_.W||s.viewport.h!=core_.H||
           s.viewport.min_z!=0||s.viewport.max_z!=1||s.scissor_count||s.ss_offset_x||s.ss_offset_y)
            throw std::logic_error("Vulkan viewport/scissor variation not integrated");
        if(s.zs.header)throw std::logic_error("External Vulkan depth surface not integrated");
        if(s.rs[gpu::kRsZEnable]||s.rs[gpu::kRsStencilEnable])throw std::logic_error("Depth/stencil requires bound guest surface");
        gpu::Assembled a{};
        if(!gpu::Assemble(s,prim,start,count,indices,a))return;
        for(unsigned i=0;i<4;++i)if(s.textures[i]){
            gpu::TextureDesc d{};
            if(!gpu::Describe(s.textures[i],d)||d.linear||d.levels!=1)
                throw std::logic_error("Vulkan bridge requires single-level swizzled/BC texture");
            uint32_t palette=s.palettes[i]?gpu::PhysicalResourceAddress(gpu::ReadGuest32(uint64_t(s.palettes[i])+4)):0;
            core_.UploadGuestTexture(s.textures[i],i,palette);
        }
        core_.Draw({},false,false,false,false,&s,false,&a,true,true);
    }
    void Copy(const gpu::Surface&,const gpu::Surface&,uint32_t,uint32_t,uint32_t,float,float)override{
        throw std::logic_error("Vulkan surface copy not integrated");
    }
    void Present(const gpu::Surface& front)override{Target(front);Writeback();}
    void Sync(uint32_t data,uint32_t size)override{
        if(target_.header&&core_.frame_valid&&uint64_t(data)<uint64_t(address_)+uint64_t(pitch_)*core_.H&&uint64_t(address_)<uint64_t(data)+size)Writeback();
    }
    void SyncLate(uint32_t data)override{Sync(data,1);}
    void Flush()override{} // every submission is synchronous
    void SetGamma(const uint8_t*)override{throw std::logic_error("Vulkan gamma not integrated");}
    void GetReport(uint32_t)override{throw std::logic_error("Vulkan visibility report not integrated");}
};
}
