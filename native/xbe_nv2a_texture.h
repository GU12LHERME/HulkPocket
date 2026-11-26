#pragma once
#include "gpu/texture.h"
#include "gpu/guest_memory.h"
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace hulkpocket {
// NV097_SET_TEXTURE_FORMAT fields, XboxDev/nxdk lib/pbkit/nv_regs.h.
// The initial RP6 texture is 08810629: DMA 1, 2D, A8R8G8B8,
// one mip, 256x256. Other layouts require separate validation.
inline gpu::TextureDesc DescribeNv2aSwizzledTexture(uint32_t offset,uint32_t format){
    if((format&3)!=1||(format&4)||((format>>4)&15)!=2||
       (format>>28)!=0||offset>=0x04000000u||offset%4)
        throw std::logic_error("Unsupported raw NV2A texture DMA or dimensionality");
    gpu::TextureDesc d{};
    d.data=0x80000000u|offset;d.format=(format>>8)&255;
    d.levels=(format>>16)&15;
    unsigned u=(format>>20)&15,v=(format>>24)&15;
    const auto& info=gpu::Format(d.format);
    if(!info.swizzled||info.host!=gpu::Host::BGRA8||d.format==0x0b||
       u>12||v>12||!d.levels||d.levels>1+std::max(u,v))
        throw std::logic_error("Unsupported raw NV2A texture format or mip layout");
    d.width=1u<<u;d.height=1u<<v;d.depth=1;d.pitch=d.width*info.bits/8;
    d.linear=false;d.cube=false;
    const auto bytes=gpu::TotalSize(d);
    if(!bytes||uint64_t(offset)+bytes>0x04000000u)
        throw std::out_of_range("Raw NV2A texture exceeds physical RAM");
    gpu::CheckedGuestRange(d.data,bytes);
    return d;
}
struct Nv2aTextureProbe {
    gpu::TextureDesc desc;
    uint32_t row_pitch=0;
    uint64_t hash=0;
};
inline Nv2aTextureProbe ProbeNv2aSwizzledTexture(uint32_t offset,uint32_t format){
    Nv2aTextureProbe result{};
    result.desc=DescribeNv2aSwizzledTexture(offset,format);
    std::vector<uint8_t> pixels;
    gpu::DecodeGuestLevel(result.desc,0,0,0,pixels,result.row_pitch);
    if(result.row_pitch!=result.desc.width*4||
       pixels.size()!=uint64_t(result.row_pitch)*result.desc.height)
        throw std::runtime_error("Invalid decoded NV2A texture extent");
    result.hash=14695981039346656037ull;
    for(auto x:pixels){result.hash^=x;result.hash*=1099511628211ull;}
    return result;
}
}
