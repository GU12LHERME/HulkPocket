#pragma once
#include "xbe_kernel_pool.h"
#include "hulk_audio_queue.h"
#include "hulk_android_audio.h"
#include "hulk_sound_spatial.h"
#include "hulk_sound_reverb.h"
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
// Integer wall-clock mixer scheduling. Preserve fractional sample time
// across calls; discard only elapsed time beyond the explicit catch-up cap.
struct HulkAudioClock {
    static constexpr uint64_t Rate=48000, NsPerSecond=1000000000, MaxNs=100000000;
    uint64_t remainder=0,dropped_ns=0;
    uint32_t Advance(uint64_t elapsed_ns){
        auto bounded=std::min(elapsed_ns,MaxNs);
        dropped_ns+=elapsed_ns-bounded;
        auto scaled=bounded*Rate+remainder;
        remainder=scaled%NsPerSecond;
        return uint32_t(scaled/NsPerSecond);
    }
};
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
    // Xbox XDK 5849: up to eight routing slots for a hardware voice,
    // 32 mix bins. Preserve slot order (stereo source alternates channels).
    struct Routing {
        uint32_t mask=0,count=0;
        std::array<uint8_t,8> slots{};
        std::array<float,32> gains{};
        Routing(){gains.fill(1.0f);}
    };
    std::array<uint8_t,32> bin_headroom_{};
    std::array<float,32> send_peak_{};
    uint32_t routing_logs_=0,routing_rejections_=0;
    struct Voice {
        Format fmt{};
        uint32_t data=0,bytes=0,owned=0,frequency=0;
        bool playing=false,loop=false,paused=false,sync=false;
        uint32_t play_start=0,play_length=0,loop_start=0,loop_length=0;
        float gain=1,headroom=1;
        Routing routing{};
        bool is3d=false;
        spatial::Deferred<spatial::Source> space{};
        spatial::Deferred<environment::SourceParameters> environment{};
        environment::SourceFilter environmental_filter{};
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
        Routing routing{};
        bool is3d=false;
        spatial::Deferred<spatial::Source> space{};
        spatial::Deferred<environment::SourceParameters> environment{};
        environment::SourceFilter environmental_filter{};
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
    spatial::Deferred<spatial::Listener> listener_{};
    std::unique_ptr<environment::Reverb> reverb_;
    environment::Parameters pending_environment_{};
    float environmental_rolloff_=0;
    bool environment_pending_=false;
    struct EffectSegment {uint32_t address=0,size=0;};
    std::vector<uint32_t> effect_allocations_;
    std::vector<EffectSegment> effect_states_;
    struct DeferredEffect {uint32_t effect,offset;std::vector<uint8_t> bytes;};
    std::vector<DeferredEffect> pending_effects_;
    size_t pending_effect_bytes_=0;
    void ApplyEnvironment(const environment::Parameters& p){
        if(!reverb_)reverb_=std::make_unique<environment::Reverb>();
        reverb_->Set(p.data());environmental_rolloff_=p[2];
    }
    void FreeEffects(){
        reverb_.reset();environment_pending_=false;environmental_rolloff_=0;
        pending_effects_.clear();pending_effect_bytes_=0;
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
    HulkAudioClock mixer_clock_{};
    uint64_t pump_calls_=0;
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
    bool SetRouting(Routing& routing,uint32_t p,uint32_t channels,bool volumes_only){
        auto reject=[&](const char* reason){
            ++routing_rejections_;
            if(routing_rejections_<=32)std::fprintf(stderr,"HULK MIXBINS REJECT ptr=%08X channels=%u volumes=%u reason=%s\n",p,channels,unsigned(volumes_only),reason);
            return false;
        };
        if(!p){
            if(volumes_only)return reject("null volumes");
            routing=Routing{};return true;
        }
        if(!Range(p,8))return reject("header range");
        auto n=mem_.Read32(p),pairs=mem_.Read32(p+4);
        if(n>(volumes_only?32u:8u)||(n&&!Range(pairs,n*8)))return reject("pair count or range");
        Routing next=volumes_only?routing:Routing{};
        uint32_t seen=0;
        for(unsigned i=0;i<n;++i){
            auto bin=mem_.Read32(pairs+8*i);
            auto mb=static_cast<int32_t>(mem_.Read32(pairs+8*i+4));
            if(bin>=32||mb< -10000||mb>0){
                if(routing_rejections_<32)std::fprintf(stderr,"HULK MIXBINS INVALID pair=%u bin=%u mb=%d\n",i,bin,mb);
                return reject("bin or volume");
            }
            auto bit=1u<<bin;
            if(volumes_only&&(seen&bit))return reject("duplicate volume");
            seen|=bit;next.gains[bin]=mb==-10000?0.0f:std::pow(10.0f,float(mb)/2000.0f);
            if(!volumes_only)next.slots[i]=uint8_t(bin);
        }
        if(!volumes_only){next.mask=seen;next.count=n;}
        routing=next;
        if(routing_logs_++<32){
            std::fprintf(stderr,"HULK MIXBINS PASS channels=%u volumes=%u count=%u mask=%08X",channels,unsigned(volumes_only),n,next.mask);
            for(unsigned i=0;i<n;++i)std::fprintf(stderr," %u:%d",mem_.Read32(pairs+8*i),int32_t(mem_.Read32(pairs+8*i+4)));
            std::fprintf(stderr," (DSP sends monitored, not executed)\n");
        }
        return true;
    }
    void Route(const Routing& r,uint32_t channels,const std::array<float,2>& value,float* out){
        if(!r.count){out[0]+=value[0];out[1]+=value[1];return;}
        constexpr float fold=0.70710678f;
        // No DSP image: upstream fallback uses the strongest non-speaker
        // send per source channel. Loaded image: retain sends as telemetry;
        // never pretend an unknown DSP effect has actually been processed.
        const bool speakers=(r.mask&0x3f7u)!=0;
        std::array<float,2> fallback{};
        for(unsigned i=0;i<r.count;++i){
            unsigned b=r.slots[i],ch=i%channels;
            float v=value[ch]*r.gains[b]*std::ldexp(1.0f,-int(bin_headroom_[b]));
            if(b>=10){send_peak_[b]=std::max(send_peak_[b],std::abs(v));}
            if(!speakers&&effect_states_.empty()){
                fallback[ch]=std::max(fallback[ch],r.gains[b]*std::ldexp(1.0f,-int(bin_headroom_[b])));
                continue;
            }
            switch(b){
            case 0:case 6:out[0]+=v;break;
            case 1:case 7:out[1]+=v;break;
            case 2:out[0]+=v*fold;out[1]+=v*fold;break;
            case 4:case 8:out[0]+=v*fold;break;
            case 5:case 9:out[1]+=v*fold;break;
            default:break; // LFE and DSP sends require their own processing
            }
        }
        if(!speakers&&effect_states_.empty()){
            const float v=value[0]*fallback[0]+(channels==2?value[1]*fallback[1]:0);
            out[0]+=v;out[1]+=v;
        }
    }
    uint32_t Source3d(spatial::Deferred<spatial::Source>& d,bool enabled,uint32_t op,const std::array<uint32_t,8>& a){
        if(!enabled)return bad;
        bool vector=op==kSoundBufferSpatialPosition||op==kSoundStreamSpatialPosition||op==kSoundBufferVelocity||op==kSoundStreamVelocity;
        auto apply=a[vector?4:2];if(apply>1)return bad;
        if(vector){
            spatial::Vec v{spatial::Float(a[1]),spatial::Float(a[2]),spatial::Float(a[3])};
            if(!spatial::Valid(v))return bad;
            bool position=op==kSoundBufferSpatialPosition||op==kSoundStreamSpatialPosition;
            d.Set(position?1:2,apply!=0,[&](auto& x){if(position)x.position=v;else x.velocity=v;});
        }else{
            float value=spatial::Float(a[1]);if(!spatial::Finite(value)||value<=0)return bad;
            bool minimum=op==kSoundBufferMinDistance||op==kSoundStreamMinDistance;
            // Validate both effective and queued ranges before updating.
            auto valid=[&](const auto& x){return minimum?value<=x.maximum:value>=x.minimum;};
            if(!valid(d.next)||(!apply&&!valid(d.live)))return bad;
            d.Set(minimum?4:8,apply!=0,[&](auto& x){if(minimum)x.minimum=value;else x.maximum=value;});
        }
        return 0;
    }
    uint32_t AllSource3d(spatial::Deferred<spatial::Source>& d,uint32_t ptr,uint32_t apply){
        if(!Range(ptr,64)||mem_.Read32(ptr)!=64||apply>1)return bad;
        auto f=[&](unsigned off){return spatial::Float(mem_.Read32(ptr+off));};
        auto v=[&](unsigned off){return spatial::Vec{f(off),f(off+4),f(off+8)};};
        auto pos=v(4),vel=v(16),cone=v(36);
        auto inside=mem_.Read32(ptr+28),outside=mem_.Read32(ptr+32);
        auto volume=static_cast<int32_t>(mem_.Read32(ptr+48));
        auto min=f(52),max=f(56);auto mode=mem_.Read32(ptr+60);
        if(!spatial::Valid(pos)||!spatial::Valid(vel)||!spatial::Valid(cone)||
           inside>outside||outside>360||volume< -10000||volume>0||
           !spatial::Finite(min)||!spatial::Finite(max)||min<=0||max<min||mode>2||
           (inside<360&&spatial::Length(cone)<1e-6f))return bad;
        d.Set(0x1f,apply!=0,[&](auto& x){
            x.position=pos;x.velocity=vel;x.cone=cone;x.inside=inside;x.outside=outside;
            x.outside_volume=volume;x.minimum=min;x.maximum=max;x.mode=mode;
        });return 0;
    }
    uint32_t RolloffCurve(spatial::Deferred<spatial::Source>& d,uint32_t ptr,uint32_t count,uint32_t apply){
        if(count>64||apply>1||(count&&!Range(ptr,count*4)))return bad;
        std::array<float,64> points{};
        for(unsigned i=0;i<count;++i){
            points[i]=spatial::Float(mem_.Read32(ptr+4*i));
            if(!std::isfinite(points[i])||points[i]<0||points[i]>1)return bad;
        }
        d.Set(0x20,apply!=0,[&](auto& x){x.curve=points;x.curve_count=count;});return 0;
    }
    uint32_t SourceEnvironment(spatial::Deferred<environment::SourceParameters>& d,uint32_t ptr,uint32_t apply){
        if(!Range(ptr,36)||apply>1)return bad;
        auto db=[&](unsigned i){return float(static_cast<int32_t>(mem_.Read32(ptr+4*i)));};
        auto f=[&](unsigned i){return spatial::Float(mem_.Read32(ptr+4*i));};
        environment::SourceParameters p{db(0),db(1),db(2),db(3),f(4),db(5),f(6),db(7),f(8)};
        if(!environment::ValidSource(p))return bad;
        d.Set(1,apply!=0,[&](auto& x){x=p;});return 0;
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
        for(unsigned b=10;b<32;++b)if(send_peak_[b]>0)std::fprintf(stderr,"HULK DSP SEND UNPROCESSED bin=%u peak=%g\n",b,double(send_peak_[b]));
        std::fprintf(stderr,"HULK MIXBINS SUMMARY accepted=%u rejected=%u\n",routing_logs_,routing_rejections_);
        if(device_||rendered_)std::fprintf(stderr,"HULK AUDIO SUMMARY frames=%llu nonzero_frames=%llu voices=%zu pumps=%llu capped_ms=%llu (software mixer)\n",
            static_cast<unsigned long long>(rendered_),static_cast<unsigned long long>(nonzero_),voices_.size(),
            static_cast<unsigned long long>(pump_calls_),static_cast<unsigned long long>(mixer_clock_.dropped_ns/1000000));
    }
    uint32_t Create(uint32_t guid,uint32_t out,uint32_t outer){
        if(guid||outer||!Range(out,4))return bad;
        if(!device_){
            device_=pool_.Allocate(0x40);
            if(!device_)return oom;
            last_=std::chrono::steady_clock::now();mixer_clock_.remainder=0;
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
    // Full HRTF remains a request only; the 3D adapter provides stereo panning.
    void SelectFullHrtf(){
        full_hrtf_requested_=true;
        std::fprintf(stderr,"HULK DSOUND FULL HRTF REQUEST (stereo spatial approximation; HRTF not implemented)\n");
    }
    bool FullHrtfRequested()const{return full_hrtf_requested_;}
    void AttachOutput(HulkAudioQueue<>* output){output_=output;}
    bool Device(uint32_t id)const{return id&&id==device_;}
    uint32_t I3dl2Listener(uint32_t id,uint32_t ptr,uint32_t apply){
        if(!Device(id)||!Range(ptr,48)||apply>1)return bad;
        environment::Parameters p{};
        for(unsigned i=0;i<p.size();++i){
            auto bits=mem_.Read32(ptr+4*i);
            p[i]=(i<2||i==5||i==7)?float(static_cast<int32_t>(bits)):spatial::Float(bits);
        }
        if(!environment::Valid(p)){
            std::fprintf(stderr,"HULK I3DL2 invalid parameters:");
            for(auto x:p)std::fprintf(stderr," %g",double(x));
            std::fprintf(stderr,"\n");return bad;
        }
        if(apply){pending_environment_=p;environment_pending_=true;}
        else{ApplyEnvironment(p);environment_pending_=false;}
        std::fprintf(stderr,"HULK I3DL2 LISTENER PASS room=%g decay=%g reverb=%g apply=%u (spatial stereo send; not full DSP chain)\n",double(p[0]),double(p[3]),double(p[7]),apply);
        return 0;
    }
    uint32_t Listener3d(uint32_t id,uint32_t op,const std::array<uint32_t,8>& a){
        if(!Device(id))return bad;
        if(op==kSoundCommit3d){
            spatial::Commit(listener_);
            if(environment_pending_){ApplyEnvironment(pending_environment_);environment_pending_=false;}
            for(auto& item:voices_){spatial::Commit(item.second.space);item.second.environment.live=item.second.environment.next;item.second.environment.dirty=0;}
            for(auto& item:streams_){spatial::Commit(item.second.space);item.second.environment.live=item.second.environment.next;item.second.environment.dirty=0;}
            return 0;
        }
        unsigned index=op==kSoundListenerOrientation?7:
            (op==kSoundListenerPosition||op==kSoundListenerVelocity)?4:2;
        if(a[index]>1)return bad;
        bool deferred=a[index]!=0;
        if(index>=4){
            spatial::Vec v{spatial::Float(a[1]),spatial::Float(a[2]),spatial::Float(a[3])};
            if(!spatial::Valid(v))return bad;
            if(op==kSoundListenerOrientation){
                spatial::Vec top{spatial::Float(a[4]),spatial::Float(a[5]),spatial::Float(a[6])};
                if(!spatial::Valid(top)||spatial::Length(v)<1e-6f||spatial::Length(top)<1e-6f)return bad;
                v=spatial::Unit(v);top=spatial::Unit(top);
                if(spatial::Length(spatial::Cross(top,v))<1e-4f)return bad;
                listener_.Set(4,deferred,[&](auto& x){x.front=v;x.top=top;});
            }else{
                bool pos=op==kSoundListenerPosition;
                listener_.Set(pos?1:2,deferred,[&](auto& x){if(pos)x.position=v;else x.velocity=v;});
            }
        }else{
            float v=spatial::Float(a[1]);
            if(!spatial::Finite(v)||v<0||(op==kSoundListenerDistance&&v==0))return bad;
            if(op==kSoundListenerDistance)listener_.Set(8,deferred,[&](auto& x){x.distance=v;});
            else if(op==kSoundListenerRolloff)listener_.Set(16,deferred,[&](auto& x){x.rolloff=v;});
            else if(op==kSoundListenerDoppler)listener_.Set(32,deferred,[&](auto& x){x.doppler=v;});
            else return bad;
        }
        return 0;
    }
    uint32_t SetMixBinHeadroom(uint32_t id,uint32_t bin,uint32_t steps){
        if(!Device(id)||bin>=32||steps>7)return bad;
        bin_headroom_[bin]=uint8_t(steps);return 0;
    }
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
        // DSFX_IMMEDIATE=0, DSFX_DEFERRED=1. Snapshot caller bytes at submission.
        if(flags>1||!Device(id)||effect>=effect_states_.size()||!size||!Range(src,size))return bad;
        auto seg=effect_states_[effect];
        if(offset>seg.size||size>seg.size-offset)return bad;
        if(flags){
            if(pending_effects_.size()>=256||size>kMaxBytes-pending_effect_bytes_)return oom;
            DeferredEffect e{effect,offset,std::vector<uint8_t>(size)};
            mem_.Read(src,e.bytes.data(),size);
            pending_effects_.push_back(std::move(e));pending_effect_bytes_+=size;return 0;
        }
        std::memmove(mem_.Base()+seg.address+offset,mem_.Base()+src,size);
        return 0;
    }
    uint32_t CommitEffectData(uint32_t id){
        if(!Device(id))return bad;
        for(const auto& e:pending_effects_){
            if(e.effect>=effect_states_.size())return bad;
            const auto seg=effect_states_[e.effect];
            if(e.offset>seg.size||e.bytes.size()>seg.size-e.offset||!Range(seg.address+e.offset,uint32_t(e.bytes.size())))return bad;
        }
        for(const auto& e:pending_effects_)mem_.Write(effect_states_[e.effect].address+e.offset,e.bytes.data(),e.bytes.size());
        pending_effects_.clear();pending_effect_bytes_=0;return 0;
    }
    uint32_t NewBuffer(uint32_t desc,uint32_t out){
        if(!device_||!Range(desc,20)||!Range(out,4)||voices_.size()>=192)return bad;
        auto bytes=mem_.Read32(desc+8),flags=mem_.Read32(desc+4);
        Format f{};
        // CTRL3D voices must be mono. Explicit DSP send routing remains unsupported.
        if(mem_.Read32(desc)<20||!Parse(mem_.Read32(desc+12),f)||bytes>kMaxBytes||bytes%f.align||
           ((flags&0x10u)&&f.channels!=1))return bad;
        Routing routing{};if(!SetRouting(routing,mem_.Read32(desc+16),f.channels,false)||((flags&0x10u)&&routing.mask))return bad;
        auto obj=pool_.Allocate(0x40);if(!obj)return oom;
        auto data=bytes?pool_.Allocate(bytes):0;
        if(bytes&&!data){pool_.Free(obj);return oom;}
        Voice v{};v.fmt=f;v.is3d=(flags&0x10u)!=0;v.routing=routing;v.data=v.owned=data;v.bytes=bytes;v.play_length=v.loop_length=bytes;
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
        case kSoundBufferAll3d:return v.is3d?AllSource3d(v.space,a[1],a[2]):bad;
        case kSoundBufferRolloffCurve:return v.is3d?RolloffCurve(v.space,a[1],a[2],a[3]):bad;
        case kSoundBufferI3dl2Source:
            return v.is3d?SourceEnvironment(v.environment,a[1],a[2]):bad;
        case kSoundBufferSpatialPosition:
        case kSoundBufferVelocity:
        case kSoundBufferMinDistance:
        case kSoundBufferMaxDistance:
            return Source3d(v.space,v.is3d,op,a);
        case kSoundBufferMixBins:
        case kSoundBufferMixBinVolumes:
            if(v.is3d&&a[1])return bad;
            return SetRouting(v.routing,a[1],v.fmt.channels,op==kSoundBufferMixBinVolumes)?0:bad;
        case kSoundBufferFormat:{
            Format f{};
            if(v.locked||v.playing||!Parse(a[1],f)||v.bytes%f.align||(f.channels==2&&v.is3d))return bad;
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
        case kSoundBufferStopEx:
            if(a[1]||a[2]||a[3])return bad; // only immediate stop; no timed/envelope stop
            [[fallthrough]];
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
        if(limit>32||!Parse(mem_.Read32(desc+8),f)||((flags&0x10u)&&f.channels!=1)||
           (callback&&!Range(callback,1)))return bad;
        Routing routing{};if(!SetRouting(routing,mem_.Read32(desc+20),f.channels,false)||((flags&0x10u)&&routing.mask))return bad;
        auto obj=pool_.Allocate(0x80);if(!obj)return oom;
        mem_.Write32(obj,vt);mem_.Write32(obj+4,vt2);
        StreamVoice v{};v.fmt=f;v.is3d=(flags&0x10u)!=0;v.routing=routing;v.limit=std::max(limit,1u);v.callback=callback;v.context=context;
        streams_.emplace(obj,std::move(v));mem_.Write32(out,obj);
        std::fprintf(stderr,"HULK DSOUND STREAM PASS obj=%08X tag=%04X channels=%u packets=%u callback=%08X\n",obj,f.tag,f.channels,limit,callback);
        return 0;
    }
    bool HasStreamCallback(uint32_t id)const{
        auto it=streams_.find(id);return it!=streams_.end()&&it->second.callback;
    }
    // Event completions are independent of title-thread callbacks. The
    // serialized kernel scheduler may signal them without entering guest code.
    bool TakeStreamEvent(StreamDone& out){
        for(auto it=stream_done_.begin();it!=stream_done_.end();++it){
            if(it->callback)continue;
            out=*it;stream_done_.erase(it);return true;
        }
        return false;
    }
    bool HasPendingPlayback()const{
        for(const auto& [id,v]:voices_)if(v.playing&&!v.paused&&!v.sync)return true;
        for(const auto& [id,v]:streams_)if(!v.paused&&!v.sync&&!v.packets.empty())return true;
        return false;
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
        case kSoundStreamI3dl2Source:
            return v.is3d?SourceEnvironment(v.environment,a[1],a[2]):bad;
        case kSoundStreamSpatialPosition:
        case kSoundStreamVelocity:
        case kSoundStreamMinDistance:
        case kSoundStreamMaxDistance:
            return Source3d(v.space,v.is3d,op,a);
        case kSoundStreamMixBins:
        case kSoundStreamMixBinVolumes:
            if(v.is3d&&a[1])return bad;
            return SetRouting(v.routing,a[1],v.fmt.channels,op==kSoundStreamMixBinVolumes)?0:bad;
        case kSoundStreamFormat:{
            Format f{};if(!Parse(a[1],f)||(f.channels==2&&v.is3d))return bad;
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
        std::vector<float> environmental_send(reverb_?frames:0);
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
            auto spatial_gains=v.is3d?spatial::Calculate(v.space.live,listener_.live):spatial::Gains{};
            auto step=uint32_t(std::max(1.0f,float(v.frequency?v.frequency:v.fmt.rate)*spatial_gains.pitch));
            const auto& pos=v.space.live.position;const auto& lp=listener_.live.position;
            auto distance=spatial::Length(v.space.live.mode==1?pos:spatial::Vec{pos[0]-lp[0],pos[1]-lp[1],pos[2]-lp[2]});
            auto env_gains=environment::SourceFilter::Calculate(v.environment.live,distance,environmental_rolloff_);
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
                std::array<float,2> sample{};
                for(unsigned ch=0;ch<2;++ch){
                    unsigned c=std::min(ch,v.fmt.channels-1);int16_t value=0;
                    if(v.fmt.tag==0x69)value=decoded[(frame%64)*v.fmt.channels+c];
                    else if(v.fmt.bits==8)value=int16_t((int(data[frame*v.fmt.align+c])-128)*256);
                    else std::memcpy(&value,data+frame*v.fmt.align+2*c,2);
                    sample[ch]=float(value)*v.gain*v.headroom;
                }
                sample[0]*=spatial_gains.left;sample[1]*=spatial_gains.right;
                if(v.is3d&&v.space.live.mode!=2){
                    auto send=v.environmental_filter.Apply(env_gains,sample);
                    mix[2*i]+=sample[0];mix[2*i+1]+=sample[1];
                    if(reverb_)environmental_send[i]+=send;
                }
                else Route(v.routing,v.fmt.channels,sample,mix.data()+2*i);
                v.phase+=step;
            }
            if(!v.loop&&v.phase>=uint64_t(total)*kRate){v.phase=uint64_t(total)*kRate;v.playing=false;}
        }
        for(auto& item:streams_){
            auto& v=item.second;
            if(v.paused||v.sync)continue;
            uint32_t cached_data=0,cached_block=UINT32_MAX;
            auto spatial_gains=v.is3d?spatial::Calculate(v.space.live,listener_.live):spatial::Gains{};
            auto step=uint32_t(std::max(1.0f,float(v.frequency?v.frequency:v.fmt.rate)*spatial_gains.pitch));
            const auto& pos=v.space.live.position;const auto& lp=listener_.live.position;
            auto distance=spatial::Length(v.space.live.mode==1?pos:spatial::Vec{pos[0]-lp[0],pos[1]-lp[1],pos[2]-lp[2]});
            auto env_gains=environment::SourceFilter::Calculate(v.environment.live,distance,environmental_rolloff_);
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
                std::array<float,2> sample{};
                for(unsigned ch=0;ch<2;++ch){
                    unsigned c=std::min(ch,v.fmt.channels-1);int16_t value=0;
                    if(v.fmt.tag==0x69)value=decoded[(frame%64)*v.fmt.channels+c];
                    else if(v.fmt.bits==8)value=int16_t((int(data[frame*v.fmt.align+c])-128)*256);
                    else std::memcpy(&value,data+frame*v.fmt.align+2*c,2);
                    sample[ch]=float(value)*v.gain*v.headroom;
                }
                sample[0]*=spatial_gains.left;sample[1]*=spatial_gains.right;
                if(v.is3d&&v.space.live.mode!=2){
                    auto send=v.environmental_filter.Apply(env_gains,sample);
                    mix[2*i]+=sample[0];mix[2*i+1]+=sample[1];
                    if(reverb_)environmental_send[i]+=send;
                }
                else Route(v.routing,v.fmt.channels,sample,mix.data()+2*i);
                v.phase+=step;
            }
            advance();
        }
        if(reverb_)environment::Mix(*reverb_,environmental_send,mix);
        std::vector<int16_t> pcm(mix.size());
        for(size_t i=0;i<mix.size();++i)pcm[i]=int16_t(std::clamp(mix[i],-32768.0f,32767.0f));
        for(unsigned i=0;i<frames;++i)if(pcm[2*i]||pcm[2*i+1])++nonzero_;
        rendered_+=frames;return pcm;
    }
    void PumpAt(std::chrono::steady_clock::time_point now){
        if(!device_)return;
        auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(now-last_).count();
        if(elapsed<1000000)return;
        // Preserve fractional frames between irregular guest calls. Bound
        // catch-up after a long pause rather than producing unbounded audio.
        uint32_t frames=mixer_clock_.Advance(uint64_t(elapsed));
        last_=now;++pump_calls_;
        if(rendered_+frames>uint64_t(kRate)*DiagnosticAudioSeconds())throw std::runtime_error("Audio diagnostic capture budget reached");
        if(!voices_.empty()||!streams_.empty()||output_||reverb_){
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
    void Pump(){PumpAt(std::chrono::steady_clock::now());}
};
}
