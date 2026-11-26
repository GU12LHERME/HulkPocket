#pragma once
#include "gpu/shaders.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace hulkpocket::vkexp {
using Matrix=std::array<float,16>;
inline Matrix Multiply(const Matrix& a,const Matrix& b){
    Matrix m{};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){
        for(unsigned k=0;k<4;++k)m[4*i+j]+=a[4*i+k]*b[4*k+j];
        if(!std::isfinite(m[4*i+j]))throw std::invalid_argument("Invalid Xbox transform");
    }
    return m;
}
inline void CopyMatrix(const Matrix& m,float* out){
    for(unsigned i=0;i<16;++i){
        if(!std::isfinite(m[i]))throw std::invalid_argument("Nonfinite Xbox matrix");
        out[i]=m[i];
    }
}
inline void FixedTransforms(const gpu::State& s,gpu::DrawConstants& dc){
    const auto wv=Multiply(s.transforms[256],s.transforms[2]);
    const auto wvp=Multiply(wv,s.transforms[3]);
    CopyMatrix(wv,dc.wv);CopyMatrix(wvp,dc.wvp);
    for(unsigned i=0;i<4;++i)CopyMatrix(s.transforms[16+i],dc.tm[i]);
    // Inverse transpose of the linear part, for row-vector normals.
    if(s.rs[gpu::kRsLighting]){
        float a=wv[0],b=wv[1],c=wv[2],d=wv[4],e=wv[5],f=wv[6],g=wv[8],h=wv[9],i=wv[10];
        const float cof[]={e*i-f*h,f*g-d*i,d*h-e*g,c*h-b*i,a*i-c*g,b*g-a*h,b*f-c*e,c*d-a*f,a*e-b*d};
        const float det=a*cof[0]+b*cof[1]+c*cof[2];
        if(!std::isfinite(det)||std::abs(det)<1e-12f)throw std::invalid_argument("Singular Xbox normal matrix");
        std::fill_n(dc.wvn,16,0.f);dc.wvn[15]=1;
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)
            dc.wvn[row*4+col]=cof[row*3+col]/det;
    }
}
}
