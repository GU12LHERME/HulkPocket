#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace hulkpocket::spatial {
using Vec=std::array<float,3>;
inline float Float(uint32_t bits){float v;std::memcpy(&v,&bits,4);return v;}
inline bool Finite(float v){return std::isfinite(v)&&std::abs(v)<=1e12f;}
inline bool Valid(const Vec& v){return Finite(v[0])&&Finite(v[1])&&Finite(v[2]);}
inline float Dot(const Vec& a,const Vec& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline Vec Cross(const Vec& a,const Vec& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline float Length(const Vec& v){return std::sqrt(Dot(v,v));}
inline Vec Unit(const Vec& v){auto n=Length(v);return n>1e-6f?Vec{v[0]/n,v[1]/n,v[2]/n}:Vec{};}
struct Source {Vec position{},velocity{};float minimum=1,maximum=1e9f;};
struct Listener {Vec position{},velocity{},front{0,0,1},top{0,1,0};float distance=1,rolloff=1,doppler=1;};
template<class T> struct Deferred {
    T live{},next{};
    uint32_t dirty=0;
    template<class F> void Set(uint32_t bit,bool deferred,F apply){
        if(deferred){apply(next);dirty|=bit;}
        else{apply(live);apply(next);dirty&=~bit;}
    }
};
inline void Commit(Deferred<Source>& d){d.live=d.next;d.dirty=0;}
inline void Commit(Deferred<Listener>& d){d.live=d.next;d.dirty=0;}
struct Gains {float left=1,right=1,pitch=1;};
inline Gains Calculate(const Source& s,const Listener& l){
    Vec delta{s.position[0]-l.position[0],s.position[1]-l.position[1],s.position[2]-l.position[2]};
    float distance=Length(delta);auto direction=Unit(delta);
    auto right=Unit(Cross(l.top,l.front));
    float pan=std::clamp(Dot(direction,right),-1.0f,1.0f);
    float range=std::clamp(distance,s.minimum,std::max(s.minimum,s.maximum));
    float attenuation=s.minimum/(s.minimum+l.rolloff*(range-s.minimum));
    // Equal-power stereo approximation; not the Xbox HRTF filter.
    float left=std::sqrt((1-pan)*0.5f)*attenuation;
    float rightgain=std::sqrt((1+pan)*0.5f)*attenuation;
    float toward=Dot(l.velocity,direction)*l.distance*l.doppler;
    float away=Dot(s.velocity,direction)*l.distance*l.doppler;
    float pitch=(343.0f+std::clamp(toward,-171.5f,171.5f))/(343.0f+std::clamp(away,-171.5f,171.5f));
    return {left,rightgain,std::clamp(pitch,0.5f,2.0f)};
}
}
