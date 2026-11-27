#pragma once
#include "gpu/shaders.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <bit>
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
    // SCDA FillTransformConstants supplies inverse-transpose normals
    // independently of lighting. Only require an invertible matrix when
    // lighting or a generated normal/reflection coordinate consumes it.
    bool need_normal=s.rs[gpu::kRsLighting]!=0;
    for(const auto& t:s.tss){
        const auto gen=(t[gpu::kTssTexCoordIndex]>>16)&7;
        need_normal=need_normal||gen==1||gen==3;
    }
    if(need_normal){
        float a=wv[0],b=wv[1],c=wv[2],d=wv[4],e=wv[5],f=wv[6],g=wv[8],h=wv[9],i=wv[10];
        const float cof[]={e*i-f*h,f*g-d*i,d*h-e*g,c*h-b*i,a*i-c*g,b*g-a*h,b*f-c*e,c*d-a*f,a*e-b*d};
        const float det=a*cof[0]+b*cof[1]+c*cof[2];
        if(!std::isfinite(det)||std::abs(det)<1e-12f)throw std::invalid_argument("Singular Xbox normal matrix");
        std::fill_n(dc.wvn,16,0.f);dc.wvn[15]=1;
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)
            dc.wvn[row*4+col]=cof[row*3+col]/det;
    }
}
// Constant layout follows the shared fixed-function shader and the verified GLES lighting model.
inline void FixedLighting(const gpu::State& s,gpu::DrawConstants& dc){
    if(!s.rs[gpu::kRsLighting])return;
    if(s.rs[gpu::kRsLighting]!=1||s.rs[gpu::kRsTwoSidedLighting]||s.rs[gpu::kRsVertexBlend]||
       s.rs[gpu::kRsFogEnable]||(s.light_enable&~255u))throw std::logic_error("Unsupported Vulkan lighting state");
    for(float x:s.material)if(!std::isfinite(x))throw std::invalid_argument("Invalid Vulkan material");
    if(s.material[16]<0)throw std::invalid_argument("Invalid Vulkan material power");
    for(unsigned n=0;n<4;++n)for(unsigned j=0;j<4;++j)dc.mat[n][j]=s.material[n*4+j];
    dc.mat[4][0]=s.material[16];
    auto amb=s.rs[gpu::kRsAmbient];
    dc.amb[0]=float((amb>>16)&255)/255;dc.amb[1]=float((amb>>8)&255)/255;
    dc.amb[2]=float(amb&255)/255;dc.amb[3]=float(amb>>24)/255;
    dc.misc[3]=s.rs[gpu::kRsSpecularEnable]?1.f:0.f;
    const auto& view=s.transforms[2];
    for(unsigned n=0;n<8;++n)if(s.light_enable&(1u<<n)){
        const auto& l=s.lights[n];if(l[0]<1||l[0]>3)throw std::invalid_argument("Missing enabled Vulkan light");
        float x[26]{};for(unsigned j=1;j<26;++j){x[j]=std::bit_cast<float>(l[j]);if(!std::isfinite(x[j]))throw std::invalid_argument("Nonfinite Vulkan light");}
        auto& o=dc.lt[n];for(unsigned k=0;k<3;++k)for(unsigned j=0;j<4;++j)o[k][j]=x[1+4*k+j];
        for(unsigned j=0;j<3;++j){
            o[3][j]=x[13]*view[j]+x[14]*view[4+j]+x[15]*view[8+j]+view[12+j];
            o[4][j]=x[16]*view[j]+x[17]*view[4+j]+x[18]*view[8+j];
        }
        if(l[0]!=1){
            const float len=std::sqrt(o[4][0]*o[4][0]+o[4][1]*o[4][1]+o[4][2]*o[4][2]);
            if(!std::isfinite(len)||len<=1e-6f)throw std::invalid_argument("Invalid Vulkan light direction");
            for(unsigned j=0;j<3;++j)o[4][j]/=len;
        }
        if(l[0]!=3&&(x[19]<0||x[21]<0||x[22]<0||x[23]<0))throw std::invalid_argument("Invalid Vulkan light attenuation");
        if(l[0]==2&&(x[24]<0||x[25]<x[24]||x[25]>3.141593f||x[20]<0))throw std::invalid_argument("Invalid Vulkan spot cone");
        o[3][3]=x[19];o[4][3]=x[20];for(unsigned j=0;j<3;++j)o[5][j]=x[21+j];
        o[6][0]=std::cos(x[24]*.5f);o[6][1]=std::cos(x[25]*.5f);
    }
}
// SCDA Vulkan FillCombinerConstants, pinned MIT revision a8180800:
// hardware simple-state registers are authoritative after ApplyPixelConstants.
inline void CombinerConstants(const gpu::State& s,gpu::DrawConstants& dc){
    auto color=[](uint32_t v,float* o){
        o[0]=float((v>>16)&255)/255;o[1]=float((v>>8)&255)/255;o[2]=float(v&255)/255;o[3]=float(v>>24)/255;
    };
    for(unsigned i=0;i<8;++i){
        color(s.simple[(gpu::kMPsConstant0+i*4)/4],dc.pc[2*i]);
        color(s.simple[(gpu::kMPsConstant1+i*4)/4],dc.pc[2*i+1]);
    }
    color(s.simple[gpu::kMPsFinalConstant0/4],dc.pc[16]);
    color(s.simple[gpu::kMPsFinalConstant1/4],dc.pc[17]);
    dc.misc[3]=s.rs[gpu::kRsSpecularEnable]?1.f:0.f;
    if(s.ps)for(unsigned i=1;i<4;++i){
        unsigned mode=(s.rs[gpu::kRsPsTextureModes]>>(5*i))&31;
        if(mode!=6&&mode!=7)continue;
        const auto& t=s.tss[i];
        constexpr unsigned matrix[]={gpu::kTssBumpEnvMat00,gpu::kTssBumpEnvMat01,gpu::kTssBumpEnvMat10,gpu::kTssBumpEnvMat11};
        for(unsigned j=0;j<4;++j){
            dc.bump[i][j]=std::bit_cast<float>(t[matrix[j]]);
            if(!std::isfinite(dc.bump[i][j]))throw std::invalid_argument("Nonfinite Xbox bump matrix");
        }
        if(mode==7){
            dc.bumpl[i][0]=std::bit_cast<float>(t[gpu::kTssBumpEnvLScale]);
            dc.bumpl[i][1]=std::bit_cast<float>(t[gpu::kTssBumpEnvLOffset]);
            if(!std::isfinite(dc.bumpl[i][0])||!std::isfinite(dc.bumpl[i][1]))
                throw std::invalid_argument("Nonfinite Xbox bump luminance");
        }
    }
    if(s.rs[gpu::kRsFogEnable]){
        auto mode=s.rs[gpu::kRsFogTableMode];
        if(mode<1||mode>3)throw std::invalid_argument("Unsupported Vulkan fog mode");
        dc.fog[0]=std::bit_cast<float>(s.rs[gpu::kRsFogStart]);
        dc.fog[1]=std::bit_cast<float>(s.rs[gpu::kRsFogEnd]);
        dc.fog[2]=std::bit_cast<float>(s.rs[gpu::kRsFogDensity]);dc.fog[3]=float(mode);
        for(float v:dc.fog)if(!std::isfinite(v))throw std::invalid_argument("Invalid Vulkan fog constant");
        if(mode==3&&dc.fog[0]==dc.fog[1])throw std::invalid_argument("Invalid linear fog range");
        color(s.rs[gpu::kRsFogColor],dc.fog_color);
    }
}
}
