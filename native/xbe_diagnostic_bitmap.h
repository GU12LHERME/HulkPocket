#pragma once
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>
namespace hulkpocket {
// Encode the actual top-down BGRA framebuffer readback as a standard
// uncompressed bottom-up 24-bit BMP. No game data goes to the repository.
inline void WriteDiagnosticBitmap(const char* path,const uint8_t* pixels,uint32_t w,uint32_t h,uint32_t pitch){
    if(!path||!*path||!pixels||!w||!h||w>4096||h>4096||pitch<uint64_t(w)*4)
        throw std::invalid_argument("Invalid bitmap source");
    uint32_t row=(w*3+3)&~3u, size=row*h;
    std::vector<uint8_t> bmp(54+size,0);
    auto put=[&](unsigned off,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)bmp[off+i]=uint8_t(v>>(8*i));};
    bmp[0]='B';bmp[1]='M';put(2,uint32_t(bmp.size()),4);put(10,54,4);
    put(14,40,4);put(18,w,4);put(22,h,4);put(26,1,2);put(28,24,2);put(34,size,4);
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
        auto src=pixels+size_t(h-1-y)*pitch+4*x;
        auto dst=bmp.data()+54+size_t(y)*row+3*x;
        dst[0]=src[0];dst[1]=src[1];dst[2]=src[2];
    }
    auto* file=std::fopen(path,"wb");
    if(!file)throw std::runtime_error("Cannot open diagnostic bitmap");
    bool ok=std::fwrite(bmp.data(),1,bmp.size(),file)==bmp.size();
    if(std::fclose(file)!=0)ok=false;
    if(!ok){std::remove(path);throw std::runtime_error("Cannot write diagnostic bitmap");}
}
}
