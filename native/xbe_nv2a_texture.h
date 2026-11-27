#pragma once
#include "gpu/texture.h"
#include "gpu/guest_memory.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace hulkpocket {
// Bound both the native Xbox bytes and the expanded RGBA mip chain
// before using the upstream 32-bit size helpers. This profile only
// supports ordinary swizzled color volumes, not BC, palette or depth.
inline uint32_t CheckedVolumeBytes(const gpu::TextureDesc& d){
    const auto& f=gpu::Format(d.format);
    auto pow2=[](uint32_t n){return n&&!(n&(n-1));};
    if(d.cube||d.linear||d.depth<=1||d.depth>256||
       !pow2(d.width)||!pow2(d.height)||!pow2(d.depth)||
       d.width>4096||d.height>4096||!d.levels||d.levels>13||
       !f.swizzled||f.compressed||f.depth||f.host!=gpu::Host::BGRA8||d.format==0x0b||!f.bits)
        throw std::invalid_argument("Unsupported Xbox color volume layout");
    unsigned max_levels=1;
    for(unsigned n=std::max({d.width,d.height,d.depth});n>1;n>>=1)++max_levels;
    if(d.levels>max_levels)throw std::invalid_argument("Invalid Xbox volume mip count");
    uint64_t raw=0,expanded=0;
    for(unsigned l=0;l<d.levels;++l){
        const uint64_t texels=uint64_t(std::max(1u,d.width>>l))*std::max(1u,d.height>>l)*std::max(1u,d.depth>>l);
        raw+=texels*f.bits/8;expanded+=texels*4;
    }
    if(!raw||raw>64ull*1024*1024||expanded>64ull*1024*1024)
        throw std::out_of_range("Xbox volume mip chain exceeds memory bound");
    return uint32_t(raw);
}
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
