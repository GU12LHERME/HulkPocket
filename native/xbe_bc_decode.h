#pragma once
#include "gpu/texture.h"
#include <array>
#include <algorithm>
#include <stdexcept>
#include <vector>
namespace hulkpocket {
// Portable fallback for BC1/2/3 (DXT1/3/5). No dependency on optional
// Android S3TC extensions. Bounds are checked before touching block data.
inline std::vector<uint8_t> DecodeXboxBc(uint32_t w,uint32_t h,gpu::Host fmt,const std::vector<uint8_t>& src){
    if(!w||!h||w>4096||h>4096)throw std::invalid_argument("Invalid BC size");
    bool bc1=fmt==gpu::Host::BC1,bc2=fmt==gpu::Host::BC2;
    if(!bc1&&!bc2&&fmt!=gpu::Host::BC3)throw std::invalid_argument("Invalid BC format");
    size_t block=bc1?8:16,bw=(w+3)/4,bh=(h+3)/4;
    if(src.size()!=bw*bh*block)throw std::invalid_argument("Truncated BC blocks");
    std::vector<uint8_t> out(size_t(w)*h*4);
    auto rgb=[](uint16_t v){uint8_t r=v>>11,g=(v>>5)&63,b=v&31;
        return std::array<uint8_t,4>{uint8_t((r<<3)|(r>>2)),uint8_t((g<<2)|(g>>4)),uint8_t((b<<3)|(b>>2)),255};};
    for(size_t by=0;by<bh;++by)for(size_t bx=0;bx<bw;++bx){
        const auto* p=src.data()+(by*bw+bx)*block;
        const auto* c=p+(bc1?0:8);
        uint16_t c0=c[0]|(uint16_t(c[1])<<8),c1=c[2]|(uint16_t(c[3])<<8);
        std::array<std::array<uint8_t,4>,4> colors{rgb(c0),rgb(c1),{},{}};
        bool four=!bc1||c0>c1;
        for(unsigned k=0;k<3;++k){
            colors[2][k]=four?(2*colors[0][k]+colors[1][k])/3:(colors[0][k]+colors[1][k])/2;
            colors[3][k]=four?(colors[0][k]+2*colors[1][k])/3:0;
        }
        colors[2][3]=255;colors[3][3]=four?255:0;
        uint32_t bits=uint32_t(c[4])|(uint32_t(c[5])<<8)|(uint32_t(c[6])<<16)|(uint32_t(c[7])<<24);
        uint64_t alpha_bits=0;std::array<uint8_t,8> alpha{};
        if(!bc1&&!bc2){
            alpha[0]=p[0];alpha[1]=p[1];
            if(alpha[0]>alpha[1])for(unsigned i=2;i<8;++i)alpha[i]=((8-i)*alpha[0]+(i-1)*alpha[1])/7;
            else{for(unsigned i=2;i<6;++i)alpha[i]=((6-i)*alpha[0]+(i-1)*alpha[1])/5;alpha[6]=0;alpha[7]=255;}
            for(unsigned i=0;i<6;++i)alpha_bits|=uint64_t(p[2+i])<<(8*i);
        }
        for(unsigned i=0;i<16;++i){
            auto x=bx*4+i%4,y=by*4+i/4;
            if(x>=w||y>=h)continue;
            auto rgba=colors[(bits>>(2*i))&3];
            if(bc2)rgba[3]=uint8_t(((p[i/2]>>(4*(i%2)))&15)*17);
            else if(!bc1)rgba[3]=alpha[(alpha_bits>>(3*i))&7];
            std::copy(rgba.begin(),rgba.end(),out.begin()+(y*w+x)*4);
        }
    }
    return out;
}
}
