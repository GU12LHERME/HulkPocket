#include "hulk_sound.h"
#include <cassert>
using namespace hulkpocket;
int main(){
    XboxGuestAddressSpace mem;
    mem.Commit(0x10000,0x10000);
    XbeKernelPool pool(mem,0x200000,0x100000);
    HulkSound s(mem,pool);
    assert(s.Create(0,0x10000,0)==0);
    auto ds=mem.Read32(0x10000);assert(s.Device(ds));
    assert(s.Speaker(ds,0x10004)==0&&mem.Read32(0x10004)==0);
    const uint8_t fmt[]={1,0,1,0,0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,0,0};
    mem.Write(0x10100,fmt,sizeof(fmt));
    const uint32_t desc[]={20,0,8,0x10100,0};
    mem.Write(0x10200,desc,sizeof(desc));
    assert(s.NewBuffer(0x10200,0x10008)==0);
    auto v=mem.Read32(0x10008);
    const int16_t pcm[]={1000,-2000,3000,-4000};
    mem.Write(0x10300,pcm,sizeof(pcm));
    assert(s.Buffer(v,kSoundBufferData,{v,0x10300,8})==0);
    assert(s.Buffer(v,kSoundBufferPlay,{v,0,0,0})==0);
    auto got=s.Render(4);
    for(unsigned i=0;i<4;++i)assert(got[2*i]==pcm[i]&&got[2*i+1]==pcm[i]);
    assert(s.Buffer(v,kSoundBufferStatus,{v,0x1000c})==0&&mem.Read32(0x1000c)==0);
    assert(s.Buffer(v,kSoundBufferPlay,{v,0,0,3})==0);
    got=s.Render(5);assert(got[8]==1000&&got[9]==1000);
    assert(s.Buffer(v,kSoundBufferVolume,{v,uint32_t(-10000)})==0);
    got=s.Render(1);assert(got[0]==0&&got[1]==0);
    assert(s.Buffer(v,kSoundBufferData,{v,0x400000,8})==0x88780032u);
    assert(s.Buffer(v,kSoundBufferRelease,{v})==0);
    const uint8_t adpcmfmt[]={0x69,0,1,0,0x80,0xbb,0,0,0x78,0x69,0,0,36,0,4,0,2,0,64,0};
    mem.Write(0x10100,adpcmfmt,sizeof(adpcmfmt));mem.Write32(0x10208,36);
    assert(s.NewBuffer(0x10200,0x10008)==0);v=mem.Read32(0x10008);
    std::array<uint8_t,36> encoded{};encoded[0]=0x34;encoded[1]=0x12;
    mem.Write(0x10300,encoded.data(),encoded.size());
    assert(s.Buffer(v,kSoundBufferData,{v,0x10300,36})==0);
    assert(s.Buffer(v,kSoundBufferPlay,{v,0,0,0})==0);
    got=s.Render(64);for(auto x:got)assert(x==0x1234);
    assert(s.Buffer(v,kSoundBufferRelease,{v})==0);
    assert(s.ReleaseDevice(ds)==0&&pool.Outstanding()==0);
    std::puts("Software DirectSound device, PCM/ADPCM, looping, volume and bounds passed");
}
