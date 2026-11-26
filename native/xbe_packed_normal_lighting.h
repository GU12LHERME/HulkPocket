#pragma once
#include "gpu/state.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace hulkpocket {
// Restricted fixed-function lighting for the observed XYZ/NORMPACKED3/UV
// declaration. Equations and D3D record layout follow the pinned MIT
// XboxXBERecomp runtime/gpu/shaders.cpp and d3d12.cpp. CPU implementation
// feeds the existing GLES interpolators; not a programmable NV2A shader.
class PackedNormalLighting {
    using V=std::array<float,3>;
    using M=std::array<float,16>;
    struct Light {unsigned type=0;V diffuse{},specular{},ambient{},position{},direction{};
        float range=0,falloff=0,a0=0,a1=0,a2=0,inner=0,outer=0;};
    M wv_{};
    std::array<float,9> normal_{};
    std::array<Light,8> lights_{};
    std::array<float,17> mat_{};
    V ambient_{};
    bool normalize_=false,local_=false,spec_=false;
    static float Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
    static V Scale(V a,float b){for(auto& x:a)x*=b;return a;}
    static V Add(V a,V b){for(unsigned i=0;i<3;++i)a[i]+=b[i];return a;}
    static V Unit(V a){auto l=std::sqrt(Dot(a,a));return l>1e-6f?Scale(a,1/l):V{};}
    static V Transform(V v,const M& m,bool point){
        V r{};for(unsigned j=0;j<3;++j)r[j]=v[0]*m[j]+v[1]*m[4+j]+v[2]*m[8+j]+(point?m[12+j]:0);return r;
    }
    static float Signed(uint32_t v,unsigned bits){
        const auto sign=1u<<(bits-1),mask=(sign<<1)-1;
        int n=int((v&mask)^sign)-int(sign);
        return std::max(-1.0f,float(n)/float(sign-1));
    }
public:
    struct Colors {std::array<float,4> diffuse{};V specular{};};
    static V Decode(uint32_t packed){return {Signed(packed,11),Signed(packed>>11,11),Signed(packed>>22,10)};}
    explicit PackedNormalLighting(const gpu::State& s):mat_(s.material){
        if(s.rs[gpu::kRsLighting]!=1||s.rs[gpu::kRsTwoSidedLighting]||
           s.rs[gpu::kRsVertexBlend]||s.rs[gpu::kRsFogEnable]||
           s.rs[gpu::kRsSpecularEnable]>1||s.rs[gpu::kRsLocalViewer]>1||
           s.rs[gpu::kRsNormalizeNormals]>1||s.rs[gpu::kRsColorVertex]>1||
           (s.light_enable&~255u))
            throw std::logic_error("Unsupported packed-normal lighting state");
        // No vertex diffuse/specular arrays: follow the upstream material
        // fallback when a requested vertex material source is absent.
        for(auto i:{gpu::kRsDiffuseMaterialSource,gpu::kRsSpecularMaterialSource,
                    gpu::kRsAmbientMaterialSource,gpu::kRsEmissiveMaterialSource})
            if(s.rs[i]>2)throw std::logic_error("Invalid material source");
        if(!std::all_of(mat_.begin(),mat_.end(),[](float f){return std::isfinite(f);})||mat_[16]<0)
            throw std::invalid_argument("Invalid lighting material");
        normalize_=s.rs[gpu::kRsNormalizeNormals]!=0;local_=s.rs[gpu::kRsLocalViewer]!=0;spec_=s.rs[gpu::kRsSpecularEnable]!=0;
        auto ambient=s.rs[gpu::kRsAmbient];
        ambient_={float((ambient>>16)&255)/255,float((ambient>>8)&255)/255,float(ambient&255)/255};
        const auto& w=s.transforms[256];const auto& v=s.transforms[2];
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)
            for(unsigned k=0;k<4;++k)wv_[4*i+j]+=w[4*i+k]*v[4*k+j];
        for(float x:wv_)if(!std::isfinite(x))throw std::invalid_argument("Invalid lighting transform");
        // Cofactor matrix = inverse transpose times determinant, for
        // row-vector normals in the same world-view space as positions.
        float a=wv_[0],b=wv_[1],c=wv_[2],d=wv_[4],e=wv_[5],f=wv_[6],g=wv_[8],h=wv_[9],i=wv_[10];
        normal_={e*i-f*h,f*g-d*i,d*h-e*g,c*h-b*i,a*i-c*g,b*g-a*h,b*f-c*e,c*d-a*f,a*e-b*d};
        float det=a*normal_[0]+b*normal_[1]+c*normal_[2];
        if(!std::isfinite(det)||std::abs(det)<1e-12f)throw std::invalid_argument("Singular normal transform");
        for(auto& x:normal_)x/=det;
        for(unsigned n=0;n<8;++n)if(s.light_enable&(1u<<n)){
            auto& o=lights_[n];const auto& l=s.lights[n];o.type=l[0];
            if(o.type<1||o.type>3)throw std::logic_error("Enabled light record missing");
            std::array<float,26> x{};for(unsigned k=1;k<26;++k){
                x[k]=std::bit_cast<float>(l[k]);if(!std::isfinite(x[k]))throw std::invalid_argument("Invalid light value");
            }
            for(unsigned k=0;k<3;++k){o.diffuse[k]=x[1+k];o.specular[k]=x[5+k];o.ambient[k]=x[9+k];}
            o.position=Transform({x[13],x[14],x[15]},v,true);
            o.direction=Unit(Transform({x[16],x[17],x[18]},v,false));
            o.range=x[19];o.falloff=x[20];o.a0=x[21];o.a1=x[22];o.a2=x[23];
            if(o.type!=3&&(o.range<0||o.a0<0||o.a1<0||o.a2<0))throw std::invalid_argument("Invalid light attenuation");
            if(o.type==2&&(x[24]<0||x[25]<x[24]||x[25]>3.141593f||o.falloff<0))
                throw std::invalid_argument("Invalid spotlight cone");
            o.inner=std::cos(x[24]*.5f);o.outer=std::cos(x[25]*.5f);
        }
    }
    Colors Evaluate(V position,uint32_t packed)const{
        auto p=Transform(position,wv_,true),n=Decode(packed);
        V vn{};for(unsigned j=0;j<3;++j)for(unsigned i=0;i<3;++i)vn[j]+=n[i]*normal_[3*i+j];
        if(normalize_)vn=Unit(vn);
        auto eye=local_?Scale(Unit(p),-1):V{0,0,1};
        V la{},ld{},ls{};
        for(const auto& light:lights_){
            if(!light.type)continue;
            V l{};float att=1;
            if(light.type==3)l=Scale(light.direction,-1);
            else{
                auto delta=Add(light.position,Scale(p,-1));float dist=std::sqrt(Dot(delta,delta));
                l=Scale(delta,1/std::max(dist,1e-6f));
                att=dist>light.range?0:1/std::max(light.a0+light.a1*dist+light.a2*dist*dist,1e-6f);
                if(light.type==2){
                    float rho=Dot(Scale(l,-1),light.direction);
                    if(rho<=light.outer)att=0;
                    else if(rho<light.inner)att*=std::pow(std::clamp((rho-light.outer)/std::max(light.inner-light.outer,1e-6f),0.f,1.f),light.falloff);
                }
            }
            float nl=std::max(Dot(vn,l),0.f);
            la=Add(la,Scale(light.ambient,att));
            ld=Add(ld,Scale(light.diffuse,att*nl));
            if(spec_&&nl>0)ls=Add(ls,Scale(light.specular,att*std::pow(std::max(Dot(vn,Unit(Add(l,eye))),0.f),mat_[16])));
        }
        Colors out{};
        for(unsigned j=0;j<3;++j){
            auto diff=mat_[12+j]+mat_[4+j]*(ambient_[j]+la[j])+mat_[j]*ld[j];
            auto spec=mat_[8+j]*ls[j];
            if(!std::isfinite(diff)||!std::isfinite(spec))throw std::invalid_argument("Nonfinite lighting result");
            out.diffuse[j]=std::clamp(diff,0.f,1.f);out.specular[j]=std::clamp(spec,0.f,1.f);
        }
        out.diffuse[3]=std::clamp(mat_[3],0.f,1.f);return out;
    }
};
}
