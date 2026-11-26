#include "../third_party/xboxxberecomp/audio/adpcm.h"
#include <array>
#include <cassert>
#include <cstdio>
using hulkpocket::audio::DecodeXboxAdpcm;
int main(){
    std::array<uint8_t,36> mono{};
    std::array<int16_t,64> out{};
    mono[0]=0x34;mono[1]=0x12;
    DecodeXboxAdpcm(mono,1,out);
    for(auto v:out)assert(v==0x1234);
    mono.fill(0);mono[4]=0x11;
    DecodeXboxAdpcm(mono,1,out);
    assert(out[0]==0&&out[1]==1&&out[2]==2&&out[63]==2);
    mono.fill(0xff);mono[0]=0;mono[1]=0x80;
    DecodeXboxAdpcm(mono,1,out);
    for(auto v:out)assert(v==-32768);
    std::array<uint8_t,72> stereo{};
    std::array<int16_t,128> samples{};
    stereo[0]=0x10;stereo[4]=0xf0;stereo[5]=0xff;
    DecodeXboxAdpcm(stereo,2,samples);
    for(unsigned i=0;i<64;++i){assert(samples[2*i]==16);assert(samples[2*i+1]==-16);}
    for(unsigned channels:{0u,2u,9u}){
        bool rejected=false;
        try{DecodeXboxAdpcm(mono,channels,out);}catch(const std::invalid_argument&){rejected=true;}
        assert(rejected);
    }
    std::puts("Xbox ADPCM: mono/stereo, nibble order, saturation and bounds pass");
}
