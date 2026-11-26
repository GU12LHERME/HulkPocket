#pragma once
#include "cpu/x86_context.h"
#include <cstdint>
#include <cstdlib>
namespace hulkpocket {
// Internal host callback IDs, never guest kernel ordinals.
constexpr uint32_t kGraphicsCreate=0x10000,kGraphicsDraw=0x10001,
    kGraphicsDrawIndexed=0x10002,kGraphicsSwap=0x10003,
    kXapiLaunchImage=0x10004,kHulkAllocatorStop=0x10005,kMissingGuestEntry=0x10006,
    kXapiInitDevices=0x10007,kXapiInputOpen=0x10008,kXapiInputClose=0x10009,
    kXapiInputGetState=0x1000a,kXapiInputSetState=0x1000b,kXapiInputGetCapabilities=0x1000c,kGraphicsClear=0x1000d,kViewportTraceEnter=0x1000e,kViewportTraceLeave=0x1000f,kGraphicsBlockOnTime=0x10010,kGraphicsVBlank=0x10011,kStartupTextTrace=0x10012,
    kFiberRead32=0x10013,kFiberWrite32=0x10014;
constexpr uint32_t kSoundCreate=0x10400;
constexpr uint32_t kSoundReleaseDevice=0x10401;
constexpr uint32_t kSoundSpeaker=0x10402;
constexpr uint32_t kSoundPump=0x10403;
constexpr uint32_t kSoundNewBuffer=0x10404;
constexpr uint32_t kSoundNewDeviceBuffer=0x10405;
constexpr uint32_t kSoundBufferRelease=0x10406;
constexpr uint32_t kSoundBufferData=0x10407;
constexpr uint32_t kSoundBufferPlay=0x10408;
constexpr uint32_t kSoundBufferStop=0x10409;
constexpr uint32_t kSoundBufferStatus=0x1040a;
constexpr uint32_t kSoundBufferVolume=0x1040b;
constexpr uint32_t kSoundBufferFrequency=0x1040c;
constexpr uint32_t kSoundBufferSetPosition=0x1040d;
constexpr uint32_t kSoundBufferGetPosition=0x1040e;
constexpr uint32_t kSoundDownloadEffects=0x1040f;
constexpr uint32_t kSoundGetEffectData=0x10410;
constexpr uint32_t kSoundFullHrtf=0x10411;
constexpr uint32_t kSoundSetEffectData=0x10412;
constexpr uint32_t kSoundBufferPause=0x10413;
constexpr uint32_t kSoundBufferPlayRegion=0x10414;
constexpr uint32_t kSoundBufferLoopRegion=0x10415;
constexpr uint32_t kSoundSynchPlayback=0x10416;
constexpr uint32_t kSoundBufferLock=0x10417,kSoundBufferUnlock=0x10418,kSoundBufferHeadroom=0x10419,kSoundBufferFormat=0x1041a;
constexpr uint32_t kSoundNewStream=0x1041b;
constexpr uint32_t kSoundNewDeviceStream=0x1041c;
constexpr uint32_t kSoundStreamAddRef=0x1041d;
constexpr uint32_t kSoundStreamRelease=0x1041e;
constexpr uint32_t kSoundStreamInfo=0x1041f;
constexpr uint32_t kSoundStreamStatus=0x10420;
constexpr uint32_t kSoundStreamProcess=0x10421;
constexpr uint32_t kSoundStreamDiscontinuity=0x10422;
constexpr uint32_t kSoundStreamFlush=0x10423;
constexpr uint32_t kSoundStreamFlushEx=0x10424;
constexpr uint32_t kSoundStreamPause=0x10425;
constexpr uint32_t kSoundStreamVolume=0x10426;
constexpr uint32_t kSoundStreamFrequency=0x10427;
constexpr uint32_t kSoundStreamHeadroom=0x10428;
constexpr uint32_t kSoundStreamFormat=0x10429;
constexpr uint32_t kSoundBufferMixBins=0x1042a,kSoundBufferMixBinVolumes=0x1042b;
constexpr uint32_t kSoundStreamMixBins=0x1042c,kSoundStreamMixBinVolumes=0x1042d;
constexpr uint32_t kSoundListenerPosition=0x1042e;
constexpr uint32_t kSoundListenerVelocity=0x1042f;
constexpr uint32_t kSoundListenerOrientation=0x10430;
constexpr uint32_t kSoundListenerDistance=0x10431;
constexpr uint32_t kSoundListenerRolloff=0x10432;
constexpr uint32_t kSoundListenerDoppler=0x10433;
constexpr uint32_t kSoundCommit3d=0x10434;
constexpr uint32_t kSoundBufferSpatialPosition=0x10435;
constexpr uint32_t kSoundBufferVelocity=0x10436;
constexpr uint32_t kSoundBufferMinDistance=0x10437;
constexpr uint32_t kSoundBufferMaxDistance=0x10438;
constexpr uint32_t kSoundStreamSpatialPosition=0x10439;
constexpr uint32_t kSoundStreamVelocity=0x1043a;
constexpr uint32_t kSoundStreamMinDistance=0x1043b;
constexpr uint32_t kSoundStreamMaxDistance=0x1043c;
constexpr uint32_t kSoundI3dl2Listener=0x1043d;
constexpr uint32_t kSoundBufferI3dl2Source=0x1043e,kSoundStreamI3dl2Source=0x1043f,kSoundCommitEffectData=0x10440;
constexpr uint32_t kSoundBufferAll3d=0x10441,kSoundBufferRolloffCurve=0x10442,kSoundBufferStopEx=0x10443;
constexpr uint32_t kSoundMixBinHeadroom=0x10444;
constexpr uint32_t kDefaultSimple=0x10100,kDefaultRender=0x10101,kDefaultTexture=0x10102,
    kDefaultMakeSpace=0x10103,kDefaultComplexBase=0x10200;
// Bounded experimental presentation loop; the final swap is presented
// but deliberately does not return to guest code.
constexpr unsigned kDiagnosticFrameLimit=60;
constexpr unsigned kDiagnosticDrawsPerFrame=8;
// Explicitly bounded longer diagnostic sessions. Reject malformed values
// by falling back to the previous conservative defaults.
inline unsigned DiagnosticBudget(const char* name,unsigned fallback,unsigned maximum){
    const char* p=std::getenv(name);
    if(!p||!*p)return fallback;
    unsigned n=0;
    for(;*p;++p){
        if(*p<'0'||*p>'9'||n>maximum/10)return fallback;
        n=n*10+unsigned(*p-'0');
        if(n>maximum)return fallback;
    }
    return n?n:fallback;
}
// The guest override and host session must agree on the same frame ceiling.
inline unsigned DiagnosticFrameLimit(){
    return DiagnosticBudget("HULK_FRAME_LIMIT",kDiagnosticFrameLimit,1800);
}
// Keep the historical floor for short sessions, with a bounded per-frame
// allowance for extended ones. The frame ceiling is at most 1800.
constexpr unsigned DiagnosticSetterLimit(unsigned frames){
    const unsigned bounded=frames>1800?1800:frames;
    return bounded*128u>16384u?bounded*128u:16384u;
}
using GraphicsCallback=void(*)(X86Context&,uint8_t*,uint32_t);
inline thread_local GraphicsCallback graphics_callback=nullptr;
class ScopedGraphicsCallback {
    GraphicsCallback old_;
public:
    explicit ScopedGraphicsCallback(GraphicsCallback cb):old_(graphics_callback){graphics_callback=cb;}
    ~ScopedGraphicsCallback(){graphics_callback=old_;}
    ScopedGraphicsCallback(const ScopedGraphicsCallback&)=delete;
    ScopedGraphicsCallback& operator=(const ScopedGraphicsCallback&)=delete;
};
}
