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
    uint32_t address_=0,pitch_=0,depth_header_=0,depth_address_=0,depth_pitch_=0;
    bool depth_valid_=false,stencil_valid_=false,rotated_=false;
    std::array<uint32_t,2> saved_{};unsigned saved_next_=0;
    void Depth(const gpu::Surface& s){
        gpu::TextureDesc d{};
        if(!s.header||!gpu::DescribeSurface(s,d)||!d.linear||d.format!=0x2e||d.width!=core_.W||d.height!=core_.H||d.pitch<uint64_t(d.width)*4)
            throw std::logic_error("Unsupported Vulkan depth target");
        gpu::CheckedGuestRange(d.data,uint64_t(d.pitch)*d.height);
        if(depth_header_&&(depth_header_!=s.header||depth_address_!=d.data))throw std::logic_error("Vulkan depth target switching not integrated");
        depth_header_=s.header;depth_address_=d.data;depth_pitch_=d.pitch;
    }
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
        if(std::find(saved_.begin(),saved_.end(),address_)==saved_.end())saved_[saved_next_++%saved_.size()]=address_;
    }
public:
    bool Init(uint32_t w,uint32_t h)override{core_.Init(w,h);return true;}
    void Clear(const gpu::State& s,uint32_t count,const uint8_t*,uint32_t flags,uint32_t color,float z,uint32_t stencil)override{
        if(count)throw std::logic_error("Vulkan rectangle clear not integrated");
        Target(s.rt);
        if(flags&3)Depth(s.zs);
        const auto requested=flags;
        if(!core_.frame_valid){
            if((flags&0xf0)!=0xf0)throw std::logic_error("Vulkan frame requires full color clear");
            if(!(flags&1))z=1;
            if(!(flags&2))stencil=0;
            flags|=3; // initialize internal attachments, do not mark guest depth/stencil valid unless requested
        }
        core_.ClearFrame(flags,color,z,stencil);rotated_=false;
        if(requested&1)depth_valid_=true;
        if(requested&2)stencil_valid_=true;
    }
    void Draw(const gpu::State& s,uint32_t prim,uint32_t start,uint32_t count,const uint8_t* indices)override{
        Target(s.rt);
        if(!core_.frame_valid)throw std::logic_error("Vulkan draw before clear");
        if(s.viewport.x||s.viewport.y||s.viewport.w!=core_.W||s.viewport.h!=core_.H||
           s.viewport.min_z!=0||s.viewport.max_z!=1||s.scissor_count||s.ss_offset_x||s.ss_offset_y)
            throw std::logic_error("Vulkan viewport/scissor variation not integrated");
        if(s.rs[gpu::kRsZEnable]||s.rs[gpu::kRsStencilEnable])Depth(s.zs);
        if(s.rs[gpu::kRsZEnable]&&!depth_valid_)throw std::logic_error("Vulkan depth requires clear");
        if(s.rs[gpu::kRsStencilEnable]&&!stencil_valid_)throw std::logic_error("Vulkan stencil requires clear");
        gpu::Assembled a{};
        if(!gpu::Assemble(s,prim,start,count,indices,a))return;
        for(unsigned i=0;i<4;++i)if(s.textures[i]){
            gpu::TextureDesc d{};
            if(!gpu::Describe(s.textures[i],d)||d.cube||d.depth!=1)
                throw std::logic_error("Vulkan bridge requires 2D texture");
            if(uint64_t(d.data)<uint64_t(address_)+uint64_t(pitch_)*core_.H&&
               uint64_t(address_)<uint64_t(d.data)+gpu::TotalSize(d))
                throw std::logic_error("Vulkan render target alias needs synchronization");
            uint32_t palette=s.palettes[i]?gpu::PhysicalResourceAddress(gpu::ReadGuest32(uint64_t(s.palettes[i])+4)):0;
            core_.UploadGuestTexture(s.textures[i],i,palette);
            core_.SetSampler(i,s);
        }
        core_.Draw({},false,false,false,false,&s,false,&a,true,true);
    }
    void Copy(const gpu::Surface&,const gpu::Surface&,uint32_t,uint32_t,uint32_t,float,float)override{
        throw std::logic_error("Vulkan surface copy not integrated");
    }
    std::array<uint8_t,4> ReadPixel(uint32_t x,uint32_t y){if(!core_.frame_valid||x>=core_.W||y>=core_.H)throw std::out_of_range("Invalid Vulkan pixel read");return core_.Pixel(x,y);}
    void ReadbackColor(const gpu::Surface& s){Target(s);Writeback();}
    void BeginRotatedFrame(const gpu::Surface& s){
        if(!target_.header||target_.header!=s.header||!core_.frame_valid)throw std::logic_error("Invalid Vulkan rotation");
        auto old=address_;target_={};Target(s);
        if(old==address_)throw std::logic_error("Vulkan rotation did not change backing");
        core_.frame_valid=false;depth_valid_=stencil_valid_=false;rotated_=true;
    }
    void RestoreRotatedColor(const gpu::Surface& s){
        Target(s);if(core_.frame_valid)return;
        if(!rotated_||std::find(saved_.begin(),saved_.end(),address_)==saved_.end())throw std::logic_error("Vulkan rotation has no saved color");
        core_.RestoreColor(gpu::CheckedGuestRange(address_,uint64_t(pitch_)*core_.H),pitch_);rotated_=false;
    }
    void Present(const gpu::Surface& front)override{ReadbackColor(front);}
    void Sync(uint32_t data,uint32_t size)override{
        if(!size)return;
        if(depth_header_&&uint64_t(data)<uint64_t(depth_address_)+uint64_t(depth_pitch_)*core_.H&&uint64_t(depth_address_)<uint64_t(data)+size)throw std::logic_error("Vulkan guest depth readback not integrated");
        if(target_.header&&uint64_t(data)<uint64_t(address_)+uint64_t(pitch_)*core_.H&&uint64_t(address_)<uint64_t(data)+size)Writeback();
    }
    void SyncLate(uint32_t data)override{Sync(data,1);}
    void Flush()override{} // every submission is synchronous
    void SetGamma(const uint8_t*)override{throw std::logic_error("Vulkan gamma not integrated");}
    void GetReport(uint32_t)override{throw std::logic_error("Vulkan visibility report not integrated");}
};
}
