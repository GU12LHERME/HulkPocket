#pragma once
#include "xbe_kernel_pool.h"
#include "xbe_kernel_physical_memory.h"
#include <array>
#include <cstring>
#include <stdexcept>
namespace hulkpocket {
// XDK 5849 CDevice storage and linear frame-buffer resource records.
// The caller supplies verified region-specific static device addresses.
// Does not execute guest defaults, push buffers or expose CreateDevice yet.
class XbeD3d8DeviceMemory {
    XboxGuestAddressSpace& mem_;
    XbeKernelPool& pool_;
    XbePhysicalMemory& physical_;
    struct Resource{uint32_t header=0,data=0;};
    std::array<Resource,4> resources_{};
    uint32_t count_=0,device_=0,slot_=0,scratch_=0;
    static constexpr uint32_t scratch_size=0x10000;
    Resource Make(uint32_t w,uint32_t h,uint32_t fmt){
        uint32_t pitch=(w*4+63)&~63u;
        if(pitch/64>256)throw std::invalid_argument("D3D pitch exceeds resource header");
        auto data=physical_.Allocate(pitch*h,0,UINT32_MAX,4096);
        if(!data)throw std::runtime_error("D3D physical surface allocation failed");
        uint32_t hdr=0;
        try{
            hdr=pool_.Allocate(24);
            if(!hdr)throw std::runtime_error("D3D surface header allocation failed");
            uint32_t words[]={0x000d0001,physical_.Physical(data),0,
                0x00010021|(fmt<<8),(w-1)|((h-1)<<12)|((pitch/64-1)<<24),0};
            mem_.Write(hdr,words,sizeof(words));
        }catch(...){if(hdr)pool_.Free(hdr);physical_.Free(data);throw;}
        return {hdr,data};
    }
    void Release(){
        for(auto& r:resources_){
            if(r.header)pool_.Free(r.header);
            if(r.data)physical_.Free(r.data);
            r={};
        }
        if(scratch_){physical_.Free(scratch_);scratch_=0;}
        count_=0;
    }
    void Set(uint32_t off,uint32_t v){mem_.Write32(device_+off,v);}
public:
    static constexpr uint32_t device_size=0x24a0;
    XbeD3d8DeviceMemory(XboxGuestAddressSpace& m,XbeKernelPool& p,XbePhysicalMemory& phys):mem_(m),pool_(p),physical_(phys){}
    XbeD3d8DeviceMemory(const XbeD3d8DeviceMemory&)=delete;
    XbeD3d8DeviceMemory& operator=(const XbeD3d8DeviceMemory&)=delete;
    ~XbeD3d8DeviceMemory(){if(device_){mem_.Write32(slot_,0);std::memset(mem_.Base()+device_,0,device_size);}Release();}
    void Initialize(uint32_t device,uint32_t slot,uint32_t pp){
        if(device_)throw std::logic_error("D3D device already allocated");
        if(!device||!slot||!mem_.IsCommitted(device,device_size)||!mem_.IsCommitted(slot,4)||!mem_.IsCommitted(pp,0x34)||
            (uint64_t(slot)<uint64_t(device)+device_size&&uint64_t(slot)+4>device))
            throw std::invalid_argument("Invalid D3D guest device records");
        uint32_t w=mem_.Read32(pp),h=mem_.Read32(pp+4),fmt=mem_.Read32(pp+8),back=mem_.Read32(pp+12),
            ms=mem_.Read32(pp+16),auto_depth=mem_.Read32(pp+0x20),depth=mem_.Read32(pp+0x24);
        if(!w)w=640;
        if(!h)h=480;
        if(!back)back=1;
        // Only formats supported by the current GLES backend.
        // Do not normalize unsupported formats or fake multisampling.
        if(w>4096||h>4096||back>2||(fmt!=6&&fmt!=0x12)||(ms!=0&&ms!=0x11)||
            auto_depth>1||(auto_depth&&depth!=0x2a&&depth!=0x2e))
            throw std::invalid_argument("Unsupported D3D present parameters");
        count_=back+1;
        try{
            for(uint32_t i=0;i<count_;++i)resources_[i]=Make(w,h,0x12);
            if(auto_depth)resources_[3]=Make(w,h,0x2e);
            scratch_=physical_.Allocate(scratch_size,0,UINT32_MAX,4096);
            if(!scratch_)throw std::runtime_error("D3D scratch allocation failed");
        }catch(...){Release();throw;}
        device_=device;slot_=slot;
        std::memset(mem_.Base()+device,0,device_size);
        mem_.Write32(slot,device);
        Set(0,scratch_);Set(4,scratch_+scratch_size-0x1000);
        Set(0x1a04,resources_[1].header);Set(0x1a08,resources_[3].header);
        Set(0x1a14,resources_[1].header);Set(0x1a18,resources_[1].header);
        Set(0x1a20,resources_[3].header);
        Set(0x948,0x4b7fffff); // 16777215.0f
        for(auto off:{0x95c,0x960,0x96c,0x970})Set(off,0x3f800000);
        Set(0xee8,w);Set(0xeec,h);Set(0xef4,0x3f800000);
        Set(0x1ddc,0x400000); // 60 Hz, no widescreen claim
        for(unsigned i=0;i<256;++i)for(unsigned channel=0;channel<3;++channel){
            uint8_t v=uint8_t(i);mem_.Write(device+0x1e04+channel*256+i,&v,1);
        }
    }
    uint32_t BackBuffer(int index)const{
        if(!device_||index< -1||index>=int(count_)-1)throw std::out_of_range("D3D buffer index");
        return resources_[index+1].header;
    }
    void BindInitialFvf(uint32_t shader){
        if(!device_||!shader||shader%4||!mem_.IsCommitted(shader,8)||
            (uint64_t(shader)<uint64_t(device_)+device_size&&uint64_t(shader)+8>device_))
            throw std::invalid_argument("Invalid initial FVF object");
        Set(0x794,shader);
    }
    uint32_t Depth()const{return resources_[3].header;}
    uint32_t DeviceAddress()const{return device_;}
    void VerifyInitialTargets()const{
        if(!device_||mem_.Read32(device_+0x1a04)!=BackBuffer(0)||
            mem_.Read32(device_+0x1a08)!=Depth())
            throw std::runtime_error("Guest switched D3D targets; target integration pending");
    }
    uint32_t Scratch()const{return scratch_;}
    uint32_t ScratchEnd()const{
        if(!device_)throw std::logic_error("D3D device not allocated");
        auto end=mem_.Read32(device_);
        if(end<scratch_||end>scratch_+scratch_size||end%4)
            throw std::out_of_range("D3D scratch pointer outside allocation");
        return end;
    }
    void RewindScratch(){
        if(!device_)throw std::logic_error("D3D device not allocated");
        Set(0,scratch_);
    }
    uint32_t BufferCount()const{return count_;}
    // Rotate physical data and lock words while preserving resource headers.
    // Called only after a successful backend present and push processing.
    uint32_t Rotate(){
        if(!device_)throw std::logic_error("D3D device not allocated");
        auto last=resources_[count_-1].header;
        auto data=mem_.Read32(last+4),lock=mem_.Read32(last+8);
        for(uint32_t i=count_-1;i>0;--i){
            auto hdr=resources_[i].header,prev=resources_[i-1].header;
            mem_.Write32(hdr+4,mem_.Read32(prev+4));mem_.Write32(hdr+8,mem_.Read32(prev+8));
        }
        auto first=resources_[0].header;
        mem_.Write32(first+4,data);mem_.Write32(first+8,lock);
        auto n=mem_.Read32(device_+0x2478)+1;Set(0x2478,n);return n;
    }
};
}
