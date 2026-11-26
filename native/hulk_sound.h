#pragma once
#include "xbe_kernel_pool.h"
#include "hulk_audio_queue.h"
#include "hulk_android_audio.h"
#include <memory>
#include <deque>
#include "xbe_graphics_boundary.h"
#include "../third_party/xboxxberecomp/audio/adpcm.h"
#include <array>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <limits>
#include <vector>
namespace hulkpocket {
// Diagnostic software DirectSound subset. All calls execute under the
// session lock. No APU MMIO, no synthetic completion for unimplemented APIs.
// Headless by default; Android diagnostic output is explicitly opt-in.
class HulkSound {
public:
    struct StreamDone {uint32_t callback,context,packet_context,status;};
private:
    XboxGuestAddressSpace& mem_;
    XbeKernelPool& pool_;
    struct Format {
        uint32_t tag=0,channels=0,rate=0,align=0,bits=0;
        uint32_t Frames(uint32_t n)const{return tag==0x69?n/align*64:n/align;}
        uint32_t Bytes(uint32_t n)const{return tag==0x69?n/64*align:n*align;}
    };
    struct Voice {
        Format fmt{};
        uint32_t data=0,bytes=0,owned=0,frequency=0;
        bool playing=false,loop=false,paused=false,sync=false;
        uint32_t play_start=0,play_length=0,loop_start=0,loop_length=0;
        float gain=1,headroom=1;
        bool locked=false;
        uint32_t lock_ptr=0,lock_bytes=0,lock_ptr2=0,lock_bytes2=0;
        uint64_t phase=0;
    };
    std::map<uint32_t,Voice> voices_;
    struct Packet {uint32_t data,bytes,completed,status,context;};
    struct StreamVoice {
        Format fmt{};
        uint32_t limit=0,callback=0,context=0,frequency=0,refs=1;
        float gain=1,headroom=1;
        bool paused=false,sync=false,ending=false;
        uint64_t phase=0;
        std::deque<Packet> packets;
    };
    std::map<uint32_t,StreamVoice> streams_;
    std::deque<StreamDone> stream_done_;
    static constexpr uint32_t pending=0x8000000au,aborted=0x80004004u;
    void Complete(StreamVoice& v,const Packet& p,uint32_t status,uint32_t used){
        if((p.completed&&!Range(p.completed,4))||(p.status&&!Range(p.status,4)))
            throw std::runtime_error("Stream completion pointer no longer committed");
        if((v.callback||p.context)&&stream_done_.size()>=1024)
            throw std::runtime_error("Stream completion queue exhausted");
        if(p.completed)mem_.Write32(p.completed,used);
        if(p.status)mem_.Write32(p.status,status);
        if(v.callback||p.context)stream_done_.push_back({v.callback,v.context,p.context,status});
    }
    void FlushStream(StreamVoice& v){
        bool first=true;
        while(!v.packets.empty()){
            auto p=v.packets.front();
            uint32_t used=first?std::min(p.bytes,v.fmt.Bytes(uint32_t(v.phase/kRate))):0;
            Complete(v,p,aborted,used);v.packets.pop_front();first=false;
        }
        v.phase=0;v.ending=false;
    }
    uint32_t device_=0,refs_=0;
    bool full_hrtf_requested_=false;
    struct EffectSegment {uint32_t address=0,size=0;};
    std::vector<uint32_t> effect_allocations_;
    std::vector<EffectSegment> effect_states_;
    void FreeEffects(){
        for(auto a:effect_allocations_)pool_.Free(a);
        effect_allocations_.clear();effect_states_.clear();
    }
    HulkAudioQueue<>* output_=nullptr; // caller owns queue until mixer is stopped
#ifdef HULK_HAS_AAUDIO_OUTPUT
    std::unique_ptr<HulkAndroidAudio> android_audio_;
#endif
    void StopAndroidOutput(){
#ifdef HULK_HAS_AAUDIO_OUTPUT
        if(android_audio_){if(output_==&android_audio_->queue)output_=nullptr;android_audio_.reset();}
#endif
    }
    uint64_t rendered_=0,nonzero_=0;
    std::chrono::steady_clock::time_point last_{};
    int capture_=-1;
    bool capture_attempted_=false;
    static constexpr uint32_t bad=0x88780032u,oom=0x8007000eu;
    static constexpr uint32_t kRate=48000,kMaxBytes=4*1024*1024;
    bool Range(uint32_t a,uint32_t n)const{
        return a&&uint64_t(a)+n<=XboxGuestAddressSpace::kSize&&mem_.IsCommitted(a,n);
    }
    bool Parse(uint32_t p,Format& f)const{
        if(!Range(p,18))return false;
        std::array<uint8_t,20> x{};mem_.Read(p,x.data(),18);
        auto u16=[&](unsigned i){return uint32_t(x[i])|(uint32_t(x[i+1])<<8);};
        f={u16(0),u16(2),uint32_t(x[4])|(uint32_t(x[5])<<8)|(uint32_t(x[6])<<16)|(uint32_t(x[7])<<24),u16(12),u16(14)};
        if(f.channels<1||f.channels>2||f.rate<8000||f.rate>48000)return false;
        if(f.tag==1)return (f.bits==8||f.bits==16)&&f.align==f.channels*f.bits/8;
        if(f.tag!=0x69||f.bits!=4||f.align!=36*f.channels||u16(16)<2||!Range(p,20))return false;
        mem_.Read(p+18,x.data()+18,2);return u16(18)==64;
    }
    void Capture(const std::vector<int16_t>& samples){
        if(!capture_attempted_){
            capture_attempted_=true;
            if(const char* p=std::getenv("HULK_AUDIO_PCM");p&&*p){
                capture_=open(p,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
                if(capture_<0)std::fprintf(stderr,"HULK AUDIO capture open failed\n");
                else std::fprintf(stderr,"HULK AUDIO capture: stereo signed 16-bit LE 48000 Hz\n");
            }
        }
        if(capture_<0)return;
        const auto* data=reinterpret_cast<const uint8_t*>(samples.data());
        size_t remain=samples.size()*sizeof(int16_t);
        while(remain){
            auto n=write(capture_,data,remain);
            if(n<0&&errno==EINTR)continue;
            if(n<=0){close(capture_);capture_=-1;std::fprintf(stderr,"HULK AUDIO capture write failed\n");return;}
            data+=n;remain-=size_t(n);
        }
    }
public:
    HulkSound(XboxGuestAddressSpace& mem,XbeKernelPool& pool):mem_(mem),pool_(pool){}
    ~HulkSound(){
        StopAndroidOutput();
        if(capture_>=0)close(capture_);
        if(device_||rendered_)std::fprintf(stderr,"HULK AUDIO SUMMARY frames=%llu nonzero_frames=%llu voices=%zu (software mixer)\n",
            static_cast<unsigned long long>(rendered_),static_cast<unsigned long long>(nonzero_),voices_.size());
    }
    uint32_t Create(uint32_t guid,uint32_t out,uint32_t outer){
        if(guid||outer||!Range(out,4))return bad;
        if(!device_){
            device_=pool_.Allocate(0x40);
            if(!device_)return oom;
            last_=std::chrono::steady_clock::now();
#ifdef HULK_HAS_AAUDIO_OUTPUT
            if(const char* mode=std::getenv("HULK_AUDIO_OUTPUT");mode&&std::strcmp(mode,"1")==0&&!output_){
                android_audio_=std::make_unique<HulkAndroidAudio>();
                output_=&android_audio_->queue;
                std::fprintf(stderr,"HULK AAUDIO enabled; waiting for mixer samples\n");
            }
#endif
            std::fprintf(stderr,"HULK DSOUND DEVICE PASS obj=%08X (software stereo)\n",device_);
        }
        if(refs_==UINT32_MAX)return bad;
        ++refs_;mem_.Write32(out,device_);return 0;
    }
    uint32_t ReleaseDevice(uint32_t id){
        if(!id||id!=device_||!refs_)return bad;
        if(--refs_)return refs_;
        if(!voices_.empty()||!streams_.empty()){++refs_;throw std::runtime_error("DirectSound release with live buffers");}
        StopAndroidOutput();FreeEffects();pool_.Free(device_);device_=0;return 0;
    }
    // Upstream 767a818 dsound.cpp uses a zero-argument HLE for this
    // selector. Record the request; our current 2D-only mixer never
    // claims to reproduce HRTF, nor accepts unsupported 3D voices.
    void SelectFullHrtf(){
        full_hrtf_requested_=true;
        std::fprintf(stderr,"HULK DSOUND FULL HRTF REQUEST (recorded; 3D processing not implemented)\n");
    }
    bool FullHrtfRequested()const{return full_hrtf_requested_;}
    void AttachOutput(HulkAudioQueue<>* output){output_=output;}
    bool Device(uint32_t id)const{return id&&id==device_;}
    uint32_t Speaker(uint32_t id,uint32_t out){
        if(!Device(id)||!Range(out,4))return bad;
        mem_.Write32(out,0);return 0; // Xbox DSSPEAKER_STEREO
    }
    // XDK 5849 layout.toml, pinned XboxXBERecomp 767a818:
    // descriptor at 0x818 + 4*(u32[0x804]+u32[0x80c]).
    // Each effect contains four address/size pairs. As in the upstream
    // descriptor adapter, reserve host-owned segments and copy plain
    // state when present; DSP code is not executed by this mixer.
    uint32_t DownloadEffects(uint32_t id,uint32_t image,uint32_t size,uint32_t loc,uint32_t out){
        if(!Device(id)||size<0x820||size>kMaxBytes||!Range(image,size)||
           (loc&&!Range(loc,4))||(out&&!Range(out,4)))return bad;
        if(!effect_allocations_.empty())throw std::runtime_error("Replacement DSP image not yet supported");
        auto read=[&](uint64_t off){return mem_.Read32(image+uint32_t(off));};
        const uint64_t table=0x818ull+4ull*(uint64_t(read(0x804))+read(0x80c));
        if(table+8>size)return bad;
        const uint32_t count=read(table);
        if(!count||count>64||table+8ull+32ull*count>size)return bad;
        uint64_t total=8+32ull*count;
        for(unsigned i=0;i<count;++i)for(unsigned j=0;j<4;++j){
            uint64_t off=table+8+32ull*i+8*j;
            total+=std::max(0x40u,read(off+4));
            if(total>kMaxBytes)return bad;
        }
        std::vector<uint32_t> alloc;
        std::vector<EffectSegment> states;
        alloc.reserve(1+4*count);states.reserve(count);
        auto rollback=[&]{for(auto a:alloc)pool_.Free(a);};
        auto allocate=[&](uint32_t n){auto a=pool_.Allocate(n);if(a)alloc.push_back(a);return a;};
        auto desc=allocate(8+32*count);
        if(!desc)return oom;
        mem_.Write32(desc,count);mem_.Write32(desc+4,read(table+4));
        unsigned copied=0,zero_states=0;
        for(unsigned i=0;i<count;++i)for(unsigned j=0;j<4;++j){
            uint64_t off=table+8+32ull*i+8*j;
            uint32_t src=read(off),n=read(off+4);
            auto dst=allocate(std::max(0x40u,n));
            if(!dst){rollback();return oom;}
            if(j==1){
                if(n&&src<=size&&n<=size-src){
                    mem_.Write(dst,mem_.Base()+image+src,n);++copied;
                }else if(n){++zero_states;}
                states.push_back({dst,n});
            }
            mem_.Write32(desc+8+32*i+8*j,dst);
            mem_.Write32(desc+12+32*i+8*j,n);
        }
        effect_allocations_=std::move(alloc);effect_states_=std::move(states);
        if(out)mem_.Write32(out,desc);
        std::fprintf(stderr,"HULK DSOUND DSP DESCRIPTOR PASS bytes=%u effects=%u table=%08X copied_states=%u zero_states=%u location=%08X (DSP processing not implemented)\n",
            size,count,uint32_t(table),copied,zero_states,loc?mem_.Read32(loc):0xffffffffu);
        return 0;
    }
    uint32_t GetEffectData(uint32_t id,uint32_t effect,uint32_t offset,uint32_t dest,uint32_t size){
        if(!Device(id)||effect>=effect_states_.size()||!Range(dest,size))return bad;
        auto seg=effect_states_[effect];
        if(offset>seg.size||size>seg.size-offset)return bad;
        std::memmove(mem_.Base()+dest,mem_.Base()+seg.address+offset,size);
        return 0;
    }
    uint32_t SetEffectData(uint32_t id,uint32_t effect,uint32_t offset,uint32_t src,uint32_t size,uint32_t flags){
        // Immediate writes only; deferred effect scheduling remains a boundary.
        if(flags!=0||!Device(id)||effect>=effect_states_.size()||!Range(src,size))return bad;
        auto seg=effect_states_[effect];
        if(offset>seg.size||size>seg.size-offset)return bad;
        std::memmove(mem_.Base()+seg.address+offset,mem_.Base()+src,size);
        return 0;
    }
    uint32_t NewBuffer(uint32_t desc,uint32_t out){
        if(!device_||!Range(desc,20)||!Range(out,4)||voices_.size()>=192)return bad;
        auto bytes=mem_.Read32(desc+8),flags=mem_.Read32(desc+4);
        Format f{};
        // Accept only plain 2D voices; no unimplemented 3D or custom bins.
        if(mem_.Read32(desc)<20||flags&0x10u||mem_.Read32(desc+16)||
           !Parse(mem_.Read32(desc+12),f)||bytes>kMaxBytes||bytes%f.align)return bad;
        auto obj=pool_.Allocate(0x40);if(!obj)return oom;
        auto data=bytes?pool_.Allocate(bytes):0;
        if(bytes&&!data){pool_.Free(obj);return oom;}
        Voice v{};v.fmt=f;v.data=v.owned=data;v.bytes=bytes;v.play_length=v.loop_length=bytes;
        voices_.emplace(obj,v);mem_.Write32(out,obj);
        std::fprintf(stderr,"HULK DSOUND BUFFER PASS obj=%08X tag=%04X channels=%u rate=%u bytes=%u flags=%08X\n",
            obj,f.tag,f.channels,f.rate,bytes,flags);return 0;
    }
    uint32_t Buffer(uint32_t id,uint32_t op,const std::array<uint32_t,8>& a){
        auto it=voices_.find(id);if(it==voices_.end())return bad;
        auto& v=it->second;
        switch(op){
        case kSoundBufferRelease:
            if(v.locked)return bad;
            if(v.owned)pool_.Free(v.owned);
            voices_.erase(it);pool_.Free(id);return 0;
        case kSoundBufferData:{
            if(v.locked)return bad;
            auto p=a[1],n=a[2];
            if(n>kMaxBytes||n%v.fmt.align||(p&&(!n||!Range(p,n))))return bad;
            if(p==v.data&&n==v.bytes)return 0;
            if(v.owned&&p>=v.owned&&uint64_t(p)<uint64_t(v.owned)+v.bytes)return bad;
            auto allocated=!p&&n?pool_.Allocate(n):0;
            if(!p&&n&&!allocated)return oom;
            if(v.owned)pool_.Free(v.owned);
            v.owned=allocated;v.data=p?p:allocated;v.bytes=n;v.phase=0;v.playing=false;v.paused=v.sync=false;
            v.play_start=v.loop_start=0;v.play_length=v.loop_length=n;return 0;
        }
        case kSoundBufferLock:{
            // XboxXBERecomp 767a818: Lock has 8 arguments including
            // this, and Xbox DSBLOCK_ENTIREBUFFER is bit 0.
            // Unsupported flags are rejected rather than guessed.
            if(v.locked||!v.bytes||!Range(v.data,v.bytes)||(a[7]&~1u))return bad;
            uint32_t off=a[1], n=(a[7]&1)?v.bytes:a[2];
            if(off>=v.bytes||!n||n>v.bytes||!Range(a[3],4)||!Range(a[4],4))return bad;
            if(bool(a[5])!=bool(a[6])||(a[5]&&(!Range(a[5],4)||!Range(a[6],4))))return bad;
            // Output words must be independent, including unaligned aliases.
            for(unsigned i=3;i<=6;++i)if(a[i])
                for(unsigned j=i+1;j<=6;++j)if(a[j]&&
                    uint64_t(a[i])<uint64_t(a[j])+4&&uint64_t(a[j])<uint64_t(a[i])+4)return bad;
            uint32_t first=std::min(n,v.bytes-off), second=n-first;
            if(second&&!a[5])return bad;
            v.lock_ptr=v.data+off;v.lock_bytes=first;
            v.lock_ptr2=second?v.data:0;v.lock_bytes2=second;v.locked=true;
            mem_.Write32(a[3],v.lock_ptr);mem_.Write32(a[4],first);
            if(a[5]){mem_.Write32(a[5],v.lock_ptr2);mem_.Write32(a[6],second);}
            return 0;
        }
        case kSoundBufferUnlock:
            if(!v.locked||a[1]!=v.lock_ptr||a[2]!=v.lock_bytes||
               a[3]!=v.lock_ptr2||a[4]!=v.lock_bytes2)return bad;
            v.locked=false;return 0;
        case kSoundBufferFormat:{
            Format f{};
            if(v.locked||v.playing||!Parse(a[1],f)||v.bytes%f.align)return bad;
            // Reset regions and phase when the unit of a sample changes.
            v.fmt=f;v.frequency=0;v.phase=0;v.paused=v.sync=false;
            v.play_start=v.loop_start=0;v.play_length=v.loop_length=v.bytes;
            return 0;
        }
        case kSoundBufferHeadroom:
            if(a[1]>10000)return bad;
            v.headroom=std::pow(10.0f,-float(a[1])/2000.0f);return 0;
        case kSoundBufferPlay:
            if(!v.play_length||!Range(v.data,v.bytes)||a[1]||a[2]||(a[3]&~7u))return bad;
            v.loop=(a[3]&1)!=0;v.sync=(a[3]&4)!=0;v.paused=false;
            if(a[3]&2||v.phase>=uint64_t(v.fmt.Frames(v.play_length))*kRate)v.phase=0;
            v.playing=true;return 0;
        case kSoundBufferStop:v.playing=false;v.paused=v.sync=false;return 0;
        case kSoundBufferPause:
            if(a[1]>2)return bad;
            v.paused=a[1]==1;v.sync=a[1]==2;return 0;
        case kSoundBufferPlayRegion:{
            if(a[1]>v.bytes||a[2]>v.bytes-a[1])return bad;
            auto n=a[2]?a[2]:v.bytes-a[1];
            if(!n||a[1]%v.fmt.align||n%v.fmt.align)return bad;
            v.play_start=a[1];v.play_length=v.loop_length=n;v.loop_start=0;v.phase=0;return 0;
        }
        case kSoundBufferLoopRegion:{
            if(a[1]>v.play_length||a[2]>v.play_length-a[1])return bad;
            auto n=a[2]?a[2]:v.play_length-a[1];
            if(!n||a[1]%v.fmt.align||n%v.fmt.align)return bad;
            v.loop_start=a[1];v.loop_length=n;return 0;
        }
        case kSoundBufferStatus:
            if(!Range(a[1],4))return bad;
            mem_.Write32(a[1],v.playing?(v.loop?5:1):0);return 0;
        case kSoundBufferVolume:{
            auto mb=static_cast<int32_t>(a[1]);if(mb< -10000||mb>0)return bad;
            v.gain=mb==-10000?0.0f:std::pow(10.0f,float(mb)/2000.0f);return 0;
        }
        case kSoundBufferFrequency:
            if(a[1]&&(a[1]<100||a[1]>200000))return bad;
            v.frequency=a[1];return 0;
        case kSoundBufferSetPosition:
            if(a[1]>=v.play_length||a[1]%v.fmt.align)return bad;
            v.phase=uint64_t(v.fmt.Frames(a[1]))*kRate;return 0;
        case kSoundBufferGetPosition:
            if((a[1]&&!Range(a[1],4))||(a[2]&&!Range(a[2],4)))return bad;
            if(a[1])mem_.Write32(a[1],v.fmt.Bytes(uint32_t(v.phase/kRate)));
            // No output queue in headless mode, so no fabricated lead cursor.
            if(a[2])mem_.Write32(a[2],v.fmt.Bytes(uint32_t(v.phase/kRate)));
            return 0;
        default:throw std::logic_error("Unsupported software buffer operation");
        }
    }
    // Pinned XDK 5849 layout: two vtable pointers at offsets 0 and 4.
    // The caller supplies original symbol addresses; no synthetic vtables.
    uint32_t NewStream(uint32_t desc,uint32_t out,uint32_t vt,uint32_t vt2){
        if(!device_||!Range(desc,24)||!Range(out,4)||!Range(vt,4)||!Range(vt2,4)||vt==vt2||streams_.size()>=64)return bad;
        Format f{};auto flags=mem_.Read32(desc),limit=mem_.Read32(desc+4);
        auto callback=mem_.Read32(desc+12),context=mem_.Read32(desc+16);
        if((flags&0x10u)||mem_.Read32(desc+20)||limit>32||!Parse(mem_.Read32(desc+8),f)||
           (callback&&!Range(callback,1)))return bad;
        auto obj=pool_.Allocate(0x80);if(!obj)return oom;
        mem_.Write32(obj,vt);mem_.Write32(obj+4,vt2);
        StreamVoice v{};v.fmt=f;v.limit=std::max(limit,1u);v.callback=callback;v.context=context;
        streams_.emplace(obj,std::move(v));mem_.Write32(out,obj);
        std::fprintf(stderr,"HULK DSOUND STREAM PASS obj=%08X tag=%04X channels=%u packets=%u callback=%08X\n",obj,f.tag,f.channels,limit,callback);
        return 0;
    }
    bool HasStreamCallback(uint32_t id)const{
        auto it=streams_.find(id);return it!=streams_.end()&&it->second.callback;
    }
    bool TakeStreamDone(StreamDone& out){
        if(stream_done_.empty())return false;
        out=stream_done_.front();stream_done_.pop_front();return true;
    }
    uint32_t Stream(uint32_t id,uint32_t op,const std::array<uint32_t,8>& a){
        auto it=streams_.find(id);if(it==streams_.end())return bad;
        auto& v=it->second;
        switch(op){
        case kSoundStreamAddRef:if(v.refs==UINT32_MAX)return bad;return ++v.refs;
        case kSoundStreamRelease:
            if(v.refs>1)return --v.refs;
            FlushStream(v);streams_.erase(it);pool_.Free(id);return 0;
        case kSoundStreamInfo:
            if(!Range(a[1],16))return bad;
            mem_.Write32(a[1],3);mem_.Write32(a[1]+4,v.fmt.align);
            mem_.Write32(a[1]+8,0);mem_.Write32(a[1]+12,0);return 0;
        case kSoundStreamStatus:
            if(!Range(a[1],4))return bad;
            mem_.Write32(a[1],v.packets.size()<v.limit?1:0);return 0;
        case kSoundStreamProcess:{
            if(a[2]||!Range(a[1],20)||v.packets.size()>=v.limit)return bad;
            auto p=a[1];
            Packet pk{mem_.Read32(p),mem_.Read32(p+4),mem_.Read32(p+8),mem_.Read32(p+12),mem_.Read32(p+16)};
            if(!pk.bytes||pk.bytes>kMaxBytes||pk.bytes%v.fmt.align||!Range(pk.data,pk.bytes)||
               (pk.completed&&!Range(pk.completed,4))||(pk.status&&!Range(pk.status,4))||
               (pk.completed&&pk.completed==pk.status))return bad;
            v.packets.push_back(pk);v.ending=false;
            if(pk.completed)mem_.Write32(pk.completed,0);
            if(pk.status)mem_.Write32(pk.status,pending);
            return 0;
        }
        case kSoundStreamDiscontinuity:v.ending=!v.packets.empty();return 0;
        case kSoundStreamFlush:FlushStream(v);return 0;
        case kSoundStreamFlushEx:
            if(a[1]||a[2]||a[3])return bad; // no scheduled/envelope flush yet
            FlushStream(v);return 0;
        case kSoundStreamPause:
            if(a[1]>2)return bad;
            v.paused=a[1]==1;v.sync=a[1]==2;return 0;
        case kSoundStreamVolume:{
            auto mb=static_cast<int32_t>(a[1]);if(mb< -10000||mb>0)return bad;
            v.gain=mb==-10000?0.0f:std::pow(10.0f,float(mb)/2000.0f);return 0;
        }
        case kSoundStreamFrequency:
            if(a[1]&&(a[1]<100||a[1]>200000))return bad;
            v.frequency=a[1];return 0;
        case kSoundStreamHeadroom:
            if(a[1]>10000)return bad;
            v.headroom=std::pow(10.0f,-float(a[1])/2000.0f);return 0;
        case kSoundStreamFormat:{
            Format f{};if(!Parse(a[1],f))return bad;
            FlushStream(v);v.fmt=f;v.frequency=0;return 0;
        }
        default:throw std::logic_error("Unsupported stream operation");
        }
    }
    uint32_t SynchPlayback(uint32_t id){
        if(!Device(id))return bad;
        for(auto& entry:voices_)entry.second.sync=false;
        for(auto& entry:streams_)entry.second.sync=false;
        return 0;
    }
    // Render exactly n frames. A single checked snapshot per voice per
    // block is decoded without reading any inaccessible guest page.
    std::vector<int16_t> Render(uint32_t frames){
        if(frames>4800)throw std::out_of_range("Audio render quantum exceeds 100ms");
        std::vector<float> mix(size_t(frames)*2);
        for(auto& [id,v]:voices_){
            (void)id;
            if(!v.playing||v.paused||v.sync||v.locked)continue;
            if(!Range(v.data,v.bytes))throw std::runtime_error("Playing sound buffer outside committed memory");
            const auto total=v.fmt.Frames(v.play_length);
            const uint64_t loop_begin=uint64_t(v.fmt.Frames(v.loop_start))*kRate;
            const uint64_t loop_span=uint64_t(v.fmt.Frames(v.loop_length))*kRate;
            const uint64_t end=v.loop?loop_begin+loop_span:uint64_t(total)*kRate;
            if(!total){v.playing=false;continue;}
            std::array<int16_t,128> decoded{};
            uint32_t cached=UINT32_MAX;
            for(unsigned i=0;i<frames;++i){
                if(v.phase>=end){
                    if(v.loop&&loop_span)v.phase=loop_begin+(v.phase-loop_begin)%loop_span;
                    else{v.phase=end;v.playing=false;break;}
                }
                uint32_t frame=uint32_t(v.phase/kRate);
                const uint8_t* data=mem_.Base()+v.data+v.play_start;
                if(v.fmt.tag==0x69&&cached!=frame/64){
                    cached=frame/64;
                    audio::DecodeXboxAdpcm(data+cached*v.fmt.align,v.fmt.align,v.fmt.channels,
                        decoded.data(),size_t(64)*v.fmt.channels);
                }
                for(unsigned ch=0;ch<2;++ch){
                    unsigned c=std::min(ch,v.fmt.channels-1);int16_t value=0;
                    if(v.fmt.tag==0x69)value=decoded[(frame%64)*v.fmt.channels+c];
                    else if(v.fmt.bits==8)value=int16_t((int(data[frame*v.fmt.align+c])-128)*256);
                    else std::memcpy(&value,data+frame*v.fmt.align+2*c,2);
                    mix[2*i+ch]+=float(value)*v.gain*v.headroom;
                }
                v.phase+=v.frequency?v.frequency:v.fmt.rate;
            }
            if(!v.loop&&v.phase>=uint64_t(total)*kRate){v.phase=uint64_t(total)*kRate;v.playing=false;}
        }
        for(auto& item:streams_){
            auto& v=item.second;
            if(v.paused||v.sync)continue;
            uint32_t cached_data=0,cached_block=UINT32_MAX;
            std::array<int16_t,128> decoded{};
            auto advance=[&]{
                while(!v.packets.empty()){
                    auto p=v.packets.front();
                    const uint64_t end=uint64_t(v.fmt.Frames(p.bytes))*kRate;
                    if(v.phase<end)break;
                    Complete(v,p,0,p.bytes);v.phase-=end;v.packets.pop_front();
                    cached_data=0;cached_block=UINT32_MAX;
                }
                if(v.packets.empty()){v.phase=0;v.ending=false;}
            };
            for(unsigned i=0;i<frames;++i){
                advance();if(v.packets.empty())break;
                auto p=v.packets.front();
                if(!Range(p.data,p.bytes))throw std::runtime_error("Playing stream packet outside committed memory");
                const uint32_t frame=uint32_t(v.phase/kRate);
                const uint8_t* data=mem_.Base()+p.data;
                if(v.fmt.tag==0x69&&(cached_data!=p.data||cached_block!=frame/64)){
                    cached_data=p.data;cached_block=frame/64;
                    audio::DecodeXboxAdpcm(data+cached_block*v.fmt.align,v.fmt.align,v.fmt.channels,
                        decoded.data(),size_t(64)*v.fmt.channels);
                }
                for(unsigned ch=0;ch<2;++ch){
                    unsigned c=std::min(ch,v.fmt.channels-1);int16_t value=0;
                    if(v.fmt.tag==0x69)value=decoded[(frame%64)*v.fmt.channels+c];
                    else if(v.fmt.bits==8)value=int16_t((int(data[frame*v.fmt.align+c])-128)*256);
                    else std::memcpy(&value,data+frame*v.fmt.align+2*c,2);
                    mix[2*i+ch]+=float(value)*v.gain*v.headroom;
                }
                v.phase+=v.frequency?v.frequency:v.fmt.rate;
            }
            advance();
        }
        std::vector<int16_t> pcm(mix.size());
        for(size_t i=0;i<mix.size();++i)pcm[i]=int16_t(std::clamp(mix[i],-32768.0f,32767.0f));
        for(unsigned i=0;i<frames;++i)if(pcm[2*i]||pcm[2*i+1])++nonzero_;
        rendered_+=frames;return pcm;
    }
    void Pump(){
        if(!device_)return;
        auto now=std::chrono::steady_clock::now();
        auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(now-last_).count();
        if(elapsed<1000)return;
        // Limit catchup and total capture duration; no invented progress
        // after a long pause. Diagnostic stops rather than consuming RAM.
        uint32_t frames=uint32_t(std::min<int64_t>(elapsed,100000)*kRate/1000000);
        last_=now;
        if(rendered_+frames>uint64_t(kRate)*120)throw std::runtime_error("Audio diagnostic capture budget reached");
        if(!voices_.empty()||!streams_.empty()||output_){
            auto pcm=Render(frames);
            Capture(pcm);
            if(output_)output_->Push(pcm.data(),frames);
#ifdef HULK_HAS_AAUDIO_OUTPUT
            if(android_audio_&&!android_audio_->Attempted()&&android_audio_->queue.Queued()>=1440){
                if(!android_audio_->Start())output_=nullptr;
            }
#endif
        }
    }
};
}
