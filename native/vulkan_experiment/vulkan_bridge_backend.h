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
    struct SavedColor {uint32_t address=0,pitch=0;};
    std::array<SavedColor,16> saved_{};unsigned saved_next_=0;
    bool Saved(uint32_t address,uint32_t pitch)const{
        return std::any_of(saved_.begin(),saved_.end(),[&](const auto& e){return e.address==address&&e.pitch==pitch;});
    }
    static bool Overlap(uint32_t a,uint64_t n,uint32_t b,uint64_t m){
        return uint64_t(a)<uint64_t(b)+m&&uint64_t(b)<uint64_t(a)+n;
    }
    void Depth(const gpu::Surface& s){
        gpu::TextureDesc d{};
        if(!s.header||!gpu::DescribeSurface(s,d)||!d.linear||d.format!=0x2e||d.width!=core_.W||d.height!=core_.H||d.pitch<uint64_t(d.width)*4)
            throw std::logic_error("Unsupported Vulkan depth target");
        gpu::CheckedGuestRange(d.data,uint64_t(d.pitch)*d.height);
        if(depth_header_&&(depth_header_!=s.header||depth_address_!=d.data||depth_pitch_!=d.pitch))
            throw std::logic_error("Vulkan depth target switching not integrated");
        if(target_.header&&Overlap(d.data,uint64_t(d.pitch)*core_.H,address_,uint64_t(pitch_)*core_.H))
            throw std::logic_error("Vulkan color/depth target overlap");
        depth_header_=s.header;depth_address_=d.data;depth_pitch_=d.pitch;
    }
    void Target(const gpu::Surface& s){
        gpu::TextureDesc d{};
        if(!s.header||!gpu::DescribeSurface(s,d)||!d.linear||d.format!=0x12||
           d.width!=core_.W||d.height!=core_.H||d.depth!=1||d.levels!=1||d.pitch<uint64_t(d.width)*4)
            throw std::invalid_argument("Unsupported Vulkan bridge target");
        gpu::CheckedGuestRange(d.data,uint64_t(d.pitch)*d.height);
        if(depth_header_&&Overlap(d.data,uint64_t(d.pitch)*core_.H,depth_address_,uint64_t(depth_pitch_)*core_.H))
            throw std::logic_error("Vulkan color target aliases depth");
        if(target_.header&&(address_!=d.data||pitch_!=d.pitch)){
            if(target_.header==s.header)
                throw std::logic_error("Vulkan same-header backing change requires verified rotation");
            if(Overlap(d.data,uint64_t(d.pitch)*core_.H,address_,uint64_t(pitch_)*core_.H))
                throw std::logic_error("Vulkan partial color target alias");
            // Finish and store the previous target before reusing the image.
            // An untouched rotated backing already resides in guest memory.
            if(core_.frame_valid)Writeback();
            core_.FlushBatch();
            core_.frame_valid=false;core_.readback_valid=false;rotated_=true;
        }
        target_=s;address_=d.data;pitch_=d.pitch;
    }
    // Rotation changes color backing, not the shared depth attachment.
    // The Vulkan images still have their initialized layouts after a swap.
    // Restore only a backing previously written to checked guest memory.
    void EnsureColor(){
        if(core_.frame_valid)return;
        if(!rotated_||!Saved(address_,pitch_))
            throw std::logic_error("Vulkan rotation has no saved color");
        const auto* pixels=gpu::CheckedGuestRange(address_,uint64_t(pitch_)*core_.H);
        core_.frame_valid=true;
        try{core_.ReplaceColor(pixels,pitch_);}
        catch(...){core_.frame_valid=false;throw;}
        rotated_=false;
    }
    void Writeback(){
        if(!target_.header)throw std::logic_error("No initialized Vulkan target");
        EnsureColor();
        StoreColor(target_,core_.ColorPixels(),core_.W,core_.H);
        if(!Saved(address_,pitch_)){
            // Invalidate overlapping older layouts; never restore a
            // differently pitched or partially aliased saved backing.
            for(auto& e:saved_)if(e.pitch&&Overlap(e.address,uint64_t(e.pitch)*core_.H,address_,uint64_t(pitch_)*core_.H))e={};
            saved_[saved_next_++%saved_.size()]={address_,pitch_};
        }
    }
public:
    bool Init(uint32_t w,uint32_t h)override{core_.Init(w,h);return true;}
    void Clear(const gpu::State& s,uint32_t count,const uint8_t* rects,uint32_t flags,uint32_t color,float z,uint32_t stencil)override{
        if(count>4096||(count&&!rects))throw std::invalid_argument("Invalid Vulkan clear rectangles");
        std::vector<VkClearRect> regions;
        for(unsigned i=0;i<count;++i){
            int32_t r[4];std::memcpy(r,rects+size_t(i)*16,16);
            if(r[0]<0||r[1]<0||r[2]<r[0]||r[3]<r[1]||uint32_t(r[2])>core_.W||uint32_t(r[3])>core_.H)
                throw std::invalid_argument("Vulkan clear rectangle outside target");
            if(r[2]!=r[0]&&r[3]!=r[1])regions.push_back({{{r[0],r[1]},{uint32_t(r[2]-r[0]),uint32_t(r[3]-r[1])}},0,1});
        }
        if(count&&regions.empty())return;
        Target(s.rt);
        if(flags&3)Depth(s.zs);
        const auto requested=flags;
        const bool full_color=!count&&(flags&0xf0)==0xf0;
        if(!core_.frame_valid&&rotated_&&!full_color)EnsureColor();
        if(count&&!core_.frame_valid)throw std::logic_error("Partial Vulkan clear before full initialization");
        const bool rotated_full=!core_.frame_valid&&rotated_&&full_color;
        if(rotated_full){
            // No need to upload old color when all of it will be replaced.
            // LOAD pass preserves depth/stencil unless explicitly cleared.
            core_.frame_valid=true;
        }else if(!core_.frame_valid){
            if(!full_color)throw std::logic_error("Vulkan frame requires full color clear");
            if(!(flags&1))z=1;
            if(!(flags&2))stencil=0;
            flags|=3; // first initialization only
        }
        try{core_.ClearFrame(flags,color,z,stencil,regions);}
        catch(...){if(rotated_full)core_.frame_valid=false;throw;}
        rotated_=false;
        // A partial clear cannot prove the untouched attachment initialized.
        if(!count&&(requested&1))depth_valid_=true;
        if(!count&&(requested&2))stencil_valid_=true;
    }
    // The bridge and core execute on the owning thread. Core copies the
    // vertex/index bytes into its per-draw upload slot before returning.
    // Do not retain any pointers into the caller's Assembled.
    void Draw(const gpu::State& s,uint32_t prim,uint32_t start,uint32_t count,const uint8_t* indices)override{
        gpu::Assembled a{};
        if(!gpu::Assemble(s,prim,start,count,indices,a))return;
        DrawAssembled(s,prim,start,count,indices,a);
    }
    void DrawAssembled(const gpu::State& s,uint32_t,uint32_t,uint32_t,const uint8_t*,const gpu::Assembled& a)override{
        if(!a.vertex_count||!(a.regs&1)||!a.buffer_count)
            throw std::invalid_argument("Invalid prepared Vulkan geometry");
        Target(s.rt);
        EnsureColor();
        const bool screen=(s.vs&&!s.vs->program.empty())||(!(s.vs_handle&1)&&(s.vs_handle&0xe)==4);
        (void)GuestViewRegion(s,core_.W,core_.H,screen);
        if(s.rs[gpu::kRsZEnable]||s.rs[gpu::kRsStencilEnable])Depth(s.zs);
        if(s.rs[gpu::kRsZEnable]&&!depth_valid_)throw std::logic_error("Vulkan depth requires clear");
        if(s.rs[gpu::kRsStencilEnable]&&!stencil_valid_)throw std::logic_error("Vulkan stencil requires clear");
        for(unsigned i=0;i<4;++i)if(s.textures[i]){
            gpu::TextureDesc d{};
            if(!gpu::Describe(s.textures[i],d)||d.cube||d.depth!=1)
                throw std::logic_error("Vulkan bridge requires 2D texture");
            auto bytes=gpu::TotalSize(d);
            if(depth_header_&&uint64_t(d.data)<uint64_t(depth_address_)+uint64_t(depth_pitch_)*core_.H&&
               uint64_t(depth_address_)<uint64_t(d.data)+bytes)
                throw std::logic_error("Vulkan depth texture alias needs depth translation");
            bool alias=uint64_t(d.data)<uint64_t(address_)+uint64_t(pitch_)*core_.H&&
                       uint64_t(address_)<uint64_t(d.data)+bytes;
            if(alias){
                if(!d.linear||d.format!=0x12||d.data!=address_||d.pitch!=pitch_||
                   d.width!=core_.W||d.height!=core_.H||d.levels!=1||s.palettes[i])
                    throw std::logic_error("Unsupported Vulkan partial or reinterpreted target texture alias");
                core_.SnapshotColorTexture(i);
            }else{
                uint32_t palette=s.palettes[i]?gpu::PhysicalResourceAddress(gpu::ReadGuest32(uint64_t(s.palettes[i])+4)):0;
                core_.UploadGuestTexture(s.textures[i],i,palette);
            }
            core_.SetSampler(i,s);
        }
        core_.Draw({},false,false,false,false,&s,false,&a,true,true,true);
    }
    void Copy(const gpu::Surface& src,const gpu::Surface& dst,uint32_t count,uint32_t rects,uint32_t points,float kx,float ky)override{
        if(kx!=1.f||ky!=1.f||count>4096||(count&&!rects)||(!count&&(rects||points)))
            throw std::invalid_argument("Unsupported Vulkan scaled or malformed copy");
        auto describe=[](const gpu::Surface& s){
            gpu::TextureDesc d{};
            if(!gpu::DescribeSurface(s,d)||!d.linear||d.format!=0x12||d.depth!=1||d.levels!=1||
               !d.width||!d.height||d.width>4096||d.height>4096||d.pitch<uint64_t(d.width)*4||
               uint64_t(d.pitch)*d.height>64*1024*1024)
                throw std::invalid_argument("Unsupported Vulkan copy surface");
            gpu::CheckedGuestRange(d.data,uint64_t(d.pitch)*d.height);
            return d;
        };
        auto a=describe(src),b=describe(dst);
        struct Region{uint32_t x,y,w,h,dx,dy;};
        std::vector<Region> regions;
        const auto* rp=count?gpu::CheckedGuestRange(rects,uint64_t(count)*16):nullptr;
        const auto* dp=points?gpu::CheckedGuestRange(points,uint64_t(count)*8):nullptr;
        for(unsigned i=0;i<(count?count:1);++i){
            int32_t r[4]={0,0,int32_t(a.width),int32_t(a.height)};
            if(rp)std::memcpy(r,rp+size_t(i)*16,16);
            int32_t d[2]={r[0],r[1]};
            if(dp)std::memcpy(d,dp+size_t(i)*8,8);
            if(r[0]<0||r[1]<0||r[2]<r[0]||r[3]<r[1]||uint32_t(r[2])>a.width||uint32_t(r[3])>a.height||
               d[0]<0||d[1]<0||uint64_t(d[0])+uint32_t(r[2]-r[0])>b.width||
               uint64_t(d[1])+uint32_t(r[3]-r[1])>b.height)
                throw std::invalid_argument("Vulkan copy rectangle outside surface");
            if(r[0]!=r[2]&&r[1]!=r[3])regions.push_back({uint32_t(r[0]),uint32_t(r[1]),uint32_t(r[2]-r[0]),
                uint32_t(r[3]-r[1]),uint32_t(d[0]),uint32_t(d[1])});
        }
        if(regions.empty())return;
        auto overlap=[](uint32_t x,uint64_t n,uint32_t y,uint64_t m){
            return uint64_t(x)<uint64_t(y)+m&&uint64_t(y)<uint64_t(x)+n;
        };
        auto active=[&](const gpu::TextureDesc& d){
            if(!target_.header||!overlap(d.data,uint64_t(d.pitch)*d.height,address_,uint64_t(pitch_)*core_.H))return false;
            if(d.data!=address_||d.pitch!=pitch_||d.width!=core_.W||d.height!=core_.H)
                throw std::logic_error("Unsupported Vulkan partial target alias");
            EnsureColor();
            return true;
        };
        if(depth_header_&&(overlap(a.data,uint64_t(a.pitch)*a.height,depth_address_,uint64_t(depth_pitch_)*core_.H)||
                          overlap(b.data,uint64_t(b.pitch)*b.height,depth_address_,uint64_t(depth_pitch_)*core_.H)))
            throw std::logic_error("Vulkan copy aliases depth");
        bool from_target=active(a),to_target=active(b);
        if(from_target||to_target)Writeback();
        const auto* source=gpu::CheckedGuestRange(a.data,uint64_t(a.pitch)*a.height);
        auto* dest=gpu::CheckedGuestRange(b.data,uint64_t(b.pitch)*b.height);
        // Snapshot before any write: self/overlapping copies cannot corrupt
        // later source rows. Keep destination pitch padding untouched.
        std::vector<uint8_t> snapshot(source,source+size_t(a.pitch)*a.height);
        for(auto r:regions)for(unsigned y=0;y<r.h;++y)
            std::memcpy(dest+size_t(r.dy+y)*b.pitch+size_t(r.dx)*4,
                        snapshot.data()+size_t(r.y+y)*a.pitch+size_t(r.x)*4,size_t(r.w)*4);
        if(to_target)core_.ReplaceColor(dest,b.pitch);
    }
    std::array<uint8_t,4> ReadPixel(uint32_t x,uint32_t y){if(!core_.frame_valid||x>=core_.W||y>=core_.H)throw std::out_of_range("Invalid Vulkan pixel read");return core_.Pixel(x,y);}
    void ReadbackColor(const gpu::Surface& s){Target(s);Writeback();}
    void BeginRotatedFrame(const gpu::Surface& s){
        if(!target_.header||target_.header!=s.header||!core_.frame_valid)throw std::logic_error("Invalid Vulkan rotation");
        core_.FlushBatch();
        auto old=address_;target_={};Target(s);
        if(old==address_)throw std::logic_error("Vulkan rotation did not change backing");
        core_.frame_valid=false;core_.readback_valid=false;rotated_=true;
        // Depth() still rejects any changed header/backing before use.
        // Retain validity of the shared depth/stencil image across color swaps.
    }
    void RestoreRotatedColor(const gpu::Surface& s){
        Target(s);EnsureColor();
    }
    void Present(const gpu::Surface& front)override{ReadbackColor(front);}
    void Sync(uint32_t data,uint32_t size)override{
        if(!size)return;
        if(depth_header_&&uint64_t(data)<uint64_t(depth_address_)+uint64_t(depth_pitch_)*core_.H&&uint64_t(depth_address_)<uint64_t(data)+size)throw std::logic_error("Vulkan guest depth readback not integrated");
        if(target_.header&&uint64_t(data)<uint64_t(address_)+uint64_t(pitch_)*core_.H&&uint64_t(address_)<uint64_t(data)+size)Writeback();
    }
    void SyncLate(uint32_t data)override{Sync(data,1);}
    void Flush()override{core_.FlushBatch();}
    uint32_t Fence()override{Flush();return gpu::Backend::Fence();}
    void WaitFence(uint32_t)override{Flush();}
    void WaitResource(uint32_t)override{Flush();}
    void WaitGpuFrame(uint64_t)override{Flush();}
    void SetGamma(const uint8_t*)override{throw std::logic_error("Vulkan gamma not integrated");}
    void GetReport(uint32_t)override{throw std::logic_error("Vulkan visibility report not integrated");}
};
}
