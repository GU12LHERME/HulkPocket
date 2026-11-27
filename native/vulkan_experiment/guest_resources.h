#pragma once
// Checked Xbox resource conversion, independent of the Vulkan device.
#include "gpu/guest_memory.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "../xbe_bc_decode.h"
#include "../xbe_nv2a_texture.h"
#include <algorithm>
#include <stdexcept>
namespace hulkpocket::vkexp {
struct TexturePixels {
    uint32_t width=0,height=0;
    std::vector<uint8_t> rgba;
    uint32_t depth=1;
};
// Only integer depth formats have the shared shadow shader's exact range.
inline uint8_t ShadowKind(uint32_t format){
    switch(format){case 0x2a:case 0x2e:return 1;case 0x2c:case 0x30:return 2;default:return 0;}
}
inline TexturePixels GuestTexture(uint32_t header,uint32_t palette=0,uint32_t level=0,uint32_t face=0){
    gpu::TextureDesc d{};
    if(!gpu::Describe(header,d)||!d.depth||(d.cube&&(d.linear||d.width!=d.height||d.depth!=1))||!d.width||!d.height||d.width>4096||d.height>4096)
        throw std::invalid_argument("Unsupported Xbox color texture");
    if(d.depth>1)(void)CheckedVolumeBytes(d);
    const auto& fmt=gpu::Format(d.format);
    if(fmt.host==gpu::Host::None||(fmt.depth&&(!ShadowKind(d.format)||d.cube||d.depth!=1||d.levels!=1||palette)))
        throw std::invalid_argument("Unsupported Xbox texture format");
    if(d.format==0x0b&&!palette)throw std::invalid_argument("Missing Xbox P8 palette");
    std::vector<uint8_t> decoded;uint32_t pitch=0;
    gpu::DecodeGuestLevel(d,level,face,palette,decoded,pitch);
    d.width=std::max(1u,d.width>>level);d.height=std::max(1u,d.height>>level);d.depth=std::max(1u,d.depth>>level);
    if(fmt.compressed)decoded=DecodeXboxBc(d.width,d.height,fmt.host,decoded);
    else if(fmt.depth){
        // DecodeGuestLevel emits normalized float32 depth, not BGRA.
        if(fmt.host!=gpu::Host::R32F||pitch!=d.width*4||decoded.size()!=size_t(d.width)*d.height*4)
            throw std::invalid_argument("Invalid decoded Xbox depth layout");
    }else{
        if(fmt.host!=gpu::Host::BGRA8||pitch!=d.width*4||decoded.size()!=size_t(d.width)*d.height*d.depth*4)
            throw std::invalid_argument("Unsupported decoded Xbox texture layout");
        for(size_t i=0;i<decoded.size();i+=4)std::swap(decoded[i],decoded[i+2]);
    }
    return {d.width,d.height,std::move(decoded),d.depth};
}
inline bool GuestDraw(const gpu::State& state,uint32_t prim,uint32_t first,uint32_t count,uint32_t index_address,gpu::Assembled& out){
    if(count>262144)throw std::invalid_argument("Xbox draw count exceeds bound");
    const auto* indices=index_address?gpu::CheckedGuestRange(index_address,uint64_t(count)*2):nullptr;
    return gpu::Assemble(state,prim,first,count,indices,out);
}
inline void StoreColor(const gpu::Surface& target,const uint8_t* rgba,uint32_t width,uint32_t height){
    gpu::TextureDesc d{};
    if(!rgba||!width||!height||width>4096||height>4096||!gpu::DescribeSurface(target,d)||!d.linear||d.format!=0x12||
       d.width!=width||d.height!=height||d.pitch<width*4)
        throw std::invalid_argument("Unsupported Xbox readback surface");
    auto* dst=gpu::CheckedGuestRange(d.data,uint64_t(d.pitch)*height);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){
        const auto* src=rgba+(size_t(y)*width+x)*4;auto* p=dst+size_t(y)*d.pitch+x*4;
        p[0]=src[2];p[1]=src[1];p[2]=src[0];p[3]=src[3];
    }
}
}
