#pragma once
#include "cpu/x86_context.h"
#include <cstdint>
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
constexpr uint32_t kDefaultSimple=0x10100,kDefaultRender=0x10101,kDefaultTexture=0x10102,
    kDefaultMakeSpace=0x10103,kDefaultComplexBase=0x10200;
// Bounded experimental presentation loop; the final swap is presented
// but deliberately does not return to guest code.
constexpr unsigned kDiagnosticFrameLimit=60;
constexpr unsigned kDiagnosticDrawsPerFrame=8;
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
