// Strong ELF overrides for actual generated game symbols. Stop at the first
// unsupported graphics boundary rather than run the original NV2A MMIO code.
#include "xbe_graphics_boundary.h"
#include "image_symbols.h"
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <initializer_list>

namespace {
#ifndef IMAGE_ABI
#define IMAGE_ABI(X)
#endif
// The checked host callback decodes the documented stdcall stack layout.
// Refuse a custom ABI rather than silently reading incorrect arguments.
#define ABI_ROW(name, ...) #name,
constexpr std::string_view mapped_names[]={IMAGE_ABI(ABI_ROW) ""};
#undef ABI_ROW
constexpr bool Stdcall(std::string_view name){
    for(auto n:mapped_names)if(n==name)return false;
    return true;
}
[[maybe_unused]] void Boundary(X86Context& ctx,uint8_t* base,uint32_t id,const char* name,bool stack_abi){
    std::fprintf(stderr,"D3D8 GUEST BOUNDARY: %s ESP=%08X EAX=%08X ECX=%08X EDX=%08X thread=%08X\n",
        name,ctx.esp,ctx.eax,ctx.ecx,ctx.edx,ctx.thread_handle);
    if(!stack_abi)throw std::runtime_error("D3D8 boundary has explicit ABI mapping; host adapter required");
    auto original_esp=ctx.esp;
    if(hulkpocket::graphics_callback)hulkpocket::graphics_callback(ctx,base,id);
    // Only explicitly integrated diagnostic boundaries may return.
    // A callback that did not complete its stdcall frame must still stop.
    if((id==hulkpocket::kGraphicsCreate||id==hulkpocket::kGraphicsClear)&&
        uint64_t(original_esp)+28==ctx.esp&&ctx.eax==0)return;
    if(id==hulkpocket::kGraphicsSwap&&uint64_t(original_esp)+8==ctx.esp&&
        ctx.eax>0&&ctx.eax<hulkpocket::kDiagnosticFrameLimit)return;
    if(id==hulkpocket::kGraphicsVBlank&&uint64_t(original_esp)+4==ctx.esp&&ctx.eax==0)return;
    if(id==hulkpocket::kGraphicsBlockOnTime&&
        uint64_t(original_esp)+12==ctx.esp&&ctx.eax==0)return;
    if(id==(hulkpocket::kGraphicsDraw|0x1000u)&&
        uint64_t(original_esp)+16==ctx.esp&&ctx.eax==0)return;
    throw std::runtime_error("D3D8 guest device integration checkpoint");
}
}
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#define GUEST_VA_(x) 0x##x##u
#define GUEST_VA(x) GUEST_VA_(x)
#ifdef IMAGE_NAMED
#define GUEST(name) CAT(sub_,name)
#else
#define GUEST(name) CAT(sub_,SYM_##name)
#endif
#define HOOK(name,id) extern "C" __attribute__((visibility("default"))) \
    void GUEST(name)(X86Context& ctx,uint8_t* base){Boundary(ctx,base,id,#name,Stdcall(#name));}
#if HAS_Direct3D_CreateDevice
extern "C" __attribute__((visibility("default")))
void GUEST(Direct3D_CreateDevice)(X86Context& ctx,uint8_t* base){
#if HAS_D3D_Device && HAS_D3D_pDevice
    ctx.ecx=GUEST_VA(SYM_D3D_Device);
    ctx.edx=GUEST_VA(SYM_D3D_pDevice);
#else
    ctx.ecx=ctx.edx=0;
#endif
#if HAS_D3D_InitDefaultState
    ctx.eax=GUEST_VA(SYM_D3D_InitDefaultState);
#else
    ctx.eax=0;
#endif
    Boundary(ctx,base,hulkpocket::kGraphicsCreate,"Direct3D_CreateDevice",Stdcall("Direct3D_CreateDevice"));
}
#else
#error "Game has no generated Direct3D_CreateDevice symbol"
#endif
#if HAS_D3DDevice_Clear
HOOK(D3DDevice_Clear,hulkpocket::kGraphicsClear)
#endif
// Supply verified generated data symbols, not guessed USA addresses.
// The guest draw ABI remains three stack arguments.
#define DRAW_HOOK(name,id) extern "C" __attribute__((visibility("default"))) \
void GUEST(name)(X86Context& ctx,uint8_t* base){ \
    std::fprintf(stderr,"D3D DRAW ORIGINAL REGISTERS EAX=%08X ECX=%08X EDX=%08X\n",ctx.eax,ctx.ecx,ctx.edx); \
    ctx.eax=GUEST_VA(SYM_D3D_Streams);ctx.ecx=GUEST_VA(SYM_D3D_RenderState); \
    ctx.edx=GUEST_VA(SYM_D3D_DeferredTextureState); \
    Boundary(ctx,base,id,#name,Stdcall(#name)); \
}
#if HAS_D3DDevice_DrawVertices
#if HAS_D3D_Streams && HAS_D3D_RenderState && HAS_D3D_DeferredTextureState
DRAW_HOOK(D3DDevice_DrawVertices,hulkpocket::kGraphicsDraw|0x1000u)
#else
HOOK(D3DDevice_DrawVertices,hulkpocket::kGraphicsDraw)
#endif
#endif
#if HAS_D3DDevice_DrawIndexedVertices
#if HAS_D3D_Streams && HAS_D3D_RenderState && HAS_D3D_DeferredTextureState
DRAW_HOOK(D3DDevice_DrawIndexedVertices,hulkpocket::kGraphicsDrawIndexed|0x1000u)
#else
HOOK(D3DDevice_DrawIndexedVertices,hulkpocket::kGraphicsDrawIndexed)
#endif
#endif
#if HAS_D3DDevice_Swap
HOOK(D3DDevice_Swap,hulkpocket::kGraphicsSwap)
#endif
#if HAS_D3DDevice_BlockUntilVerticalBlank
HOOK(D3DDevice_BlockUntilVerticalBlank,hulkpocket::kGraphicsVBlank)
#endif

#if HAS_XLaunchNewImageA
// Unlike the graphics checkpoints, a refused external launch returns a
// normal Win32 error. The host validates the guest stack and launch path.
extern "C" __attribute__((visibility("default")))
void GUEST(XLaunchNewImageA)(X86Context& ctx,uint8_t* base){
    if(!Stdcall("XLaunchNewImageA")||!hulkpocket::graphics_callback)
        throw std::runtime_error("XLaunchNewImageA requires a checked host adapter");
    hulkpocket::graphics_callback(ctx,base,hulkpocket::kXapiLaunchImage);
}
#endif

/* XAPI high-level replacement, based on the pinned XboxXBERecomp xapi.cpp.
 * The diagnostic host has no controller provider; report disconnected ports.
 * Never execute USB/OHCI hardware initialization on the Android host. */
#define XAPI_HOOK(name,id) extern "C" __attribute__((visibility("default"))) \
    void GUEST(name)(X86Context& ctx,uint8_t* base){ \
        if(!Stdcall(#name)||!hulkpocket::graphics_callback) \
            throw std::runtime_error("XAPI requires a checked host adapter: " #name); \
        hulkpocket::graphics_callback(ctx,base,id); \
    }
#if HAS_XInitDevices && HAS_XPP_GamepadType
extern "C" __attribute__((visibility("default")))
void GUEST(XInitDevices)(X86Context& ctx,uint8_t* base){
    if(!Stdcall("XInitDevices")||!hulkpocket::graphics_callback)
        throw std::runtime_error("XInitDevices requires a checked host adapter");
    // Pass generated, region-specific data addresses in volatile registers.
    // No change to the original guest stdcall stack layout.
    ctx.ecx=GUEST_VA(SYM_XPP_GamepadType);
#if HAS_XPP_MemoryUnitType
    ctx.edx=GUEST_VA(SYM_XPP_MemoryUnitType);
#else
    ctx.edx=0;
#endif
    hulkpocket::graphics_callback(ctx,base,hulkpocket::kXapiInitDevices);
}
#endif
#if HAS_XInputOpen
XAPI_HOOK(XInputOpen,hulkpocket::kXapiInputOpen)
#endif
#if HAS_XInputClose
XAPI_HOOK(XInputClose,hulkpocket::kXapiInputClose)
#endif
#if HAS_XInputGetState
XAPI_HOOK(XInputGetState,hulkpocket::kXapiInputGetState)
#endif
#if HAS_XInputSetState
XAPI_HOOK(XInputSetState,hulkpocket::kXapiInputSetState)
#endif
#if HAS_XInputGetCapabilities
XAPI_HOOK(XInputGetCapabilities,hulkpocket::kXapiInputGetCapabilities)
#endif

/* Capture original default-state setters without executing hardware MMIO.
 * Only the explicitly active diagnostic initializer may use these hooks. */
#define DEFAULT_HOOK(name,id) extern "C" __attribute__((visibility("default"))) \
    void GUEST(name)(X86Context& ctx,uint8_t* base){ \
        if(!Stdcall(#name)||!hulkpocket::graphics_callback) \
            throw std::runtime_error("Unsupported default-state ABI: " #name); \
        hulkpocket::graphics_callback(ctx,base,id); \
    }
#if HAS_D3DDevice_SetRenderState_Simple
// Upstream XDK ABI is fastcall ECX=method, EDX=value, no stack args.
DEFAULT_HOOK(D3DDevice_SetRenderState_Simple,hulkpocket::kDefaultSimple)
#endif
#if HAS_D3DDevice_SetRenderStateNotInline && HAS_D3D_SimpleRenderStateEncode && HAS_D3D_RenderState
extern "C" __attribute__((visibility("default")))
void GUEST(D3DDevice_SetRenderStateNotInline)(X86Context& ctx,uint8_t* base){
    if(!Stdcall("D3DDevice_SetRenderStateNotInline")||!hulkpocket::graphics_callback)
        throw std::runtime_error("Unsupported render-state ABI");
    ctx.ecx=GUEST_VA(SYM_D3D_SimpleRenderStateEncode);
    ctx.edx=GUEST_VA(SYM_D3D_RenderState);
    hulkpocket::graphics_callback(ctx,base,hulkpocket::kDefaultRender);
}
#endif
#if HAS_D3DDevice_SetTextureStageStateNotInline && HAS_D3D_DeferredTextureState
extern "C" __attribute__((visibility("default")))
void GUEST(D3DDevice_SetTextureStageStateNotInline)(X86Context& ctx,uint8_t* base){
    if(!Stdcall("D3DDevice_SetTextureStageStateNotInline")||!hulkpocket::graphics_callback)
        throw std::runtime_error("Unsupported texture-state ABI");
    ctx.ecx=GUEST_VA(SYM_D3D_DeferredTextureState);
    hulkpocket::graphics_callback(ctx,base,hulkpocket::kDefaultTexture);
}
#endif
#define COMPLEX_HOOK(name,index) extern "C" __attribute__((visibility("default"))) \
    void GUEST(name)(X86Context& ctx,uint8_t* base){ \
        if(!Stdcall(#name)||!hulkpocket::graphics_callback) \
            throw std::runtime_error("Unsupported complex-state ABI: " #name); \
        ctx.edx=GUEST_VA(SYM_D3D_RenderState); \
        hulkpocket::graphics_callback(ctx,base,hulkpocket::kDefaultComplexBase+index); \
    }
#if HAS_D3D_RenderState
#if HAS_D3DDevice_SetRenderState_PSTextureModes
COMPLEX_HOOK(D3DDevice_SetRenderState_PSTextureModes,136)
#endif
#if HAS_D3DDevice_SetRenderState_VertexBlend
COMPLEX_HOOK(D3DDevice_SetRenderState_VertexBlend,137)
#endif
#if HAS_D3DDevice_SetRenderState_FogColor
COMPLEX_HOOK(D3DDevice_SetRenderState_FogColor,138)
#endif
#if HAS_D3DDevice_SetRenderState_FillMode
COMPLEX_HOOK(D3DDevice_SetRenderState_FillMode,139)
#endif
#if HAS_D3DDevice_SetRenderState_BackFillMode
COMPLEX_HOOK(D3DDevice_SetRenderState_BackFillMode,140)
#endif
#if HAS_D3DDevice_SetRenderState_TwoSidedLighting
COMPLEX_HOOK(D3DDevice_SetRenderState_TwoSidedLighting,141)
#endif
#if HAS_D3DDevice_SetRenderState_NormalizeNormals
COMPLEX_HOOK(D3DDevice_SetRenderState_NormalizeNormals,142)
#endif
#if HAS_D3DDevice_SetRenderState_ZEnable
COMPLEX_HOOK(D3DDevice_SetRenderState_ZEnable,143)
#endif
#if HAS_D3DDevice_SetRenderState_StencilEnable
COMPLEX_HOOK(D3DDevice_SetRenderState_StencilEnable,144)
#endif
#if HAS_D3DDevice_SetRenderState_StencilFail
COMPLEX_HOOK(D3DDevice_SetRenderState_StencilFail,145)
#endif
#if HAS_D3DDevice_SetRenderState_FrontFace
COMPLEX_HOOK(D3DDevice_SetRenderState_FrontFace,146)
#endif
#if HAS_D3DDevice_SetRenderState_CullMode
COMPLEX_HOOK(D3DDevice_SetRenderState_CullMode,147)
#endif
#if HAS_D3DDevice_SetRenderState_TextureFactor
COMPLEX_HOOK(D3DDevice_SetRenderState_TextureFactor,148)
#endif
#if HAS_D3DDevice_SetRenderState_LogicOp
COMPLEX_HOOK(D3DDevice_SetRenderState_LogicOp,150)
#endif
#if HAS_D3DDevice_SetRenderState_EdgeAntiAlias
COMPLEX_HOOK(D3DDevice_SetRenderState_EdgeAntiAlias,151)
#endif
#if HAS_D3DDevice_SetRenderState_MultiSampleAntiAlias
COMPLEX_HOOK(D3DDevice_SetRenderState_MultiSampleAntiAlias,152)
#endif
#if HAS_D3DDevice_SetRenderState_MultiSampleMask
COMPLEX_HOOK(D3DDevice_SetRenderState_MultiSampleMask,153)
#endif
#if HAS_D3DDevice_SetRenderState_MultiSampleMode
COMPLEX_HOOK(D3DDevice_SetRenderState_MultiSampleMode,154)
#endif
#if HAS_D3DDevice_SetRenderState_MultiSampleRenderTargetMode
COMPLEX_HOOK(D3DDevice_SetRenderState_MultiSampleRenderTargetMode,155)
#endif
#if HAS_D3DDevice_SetRenderState_ShadowFunc
COMPLEX_HOOK(D3DDevice_SetRenderState_ShadowFunc,156)
#endif
#if HAS_D3DDevice_SetRenderState_LineWidth
COMPLEX_HOOK(D3DDevice_SetRenderState_LineWidth,157)
#endif
#if HAS_D3DDevice_SetRenderState_SampleAlpha
COMPLEX_HOOK(D3DDevice_SetRenderState_SampleAlpha,158)
#endif
#if HAS_D3DDevice_SetRenderState_Dxt1NoiseEnable
COMPLEX_HOOK(D3DDevice_SetRenderState_Dxt1NoiseEnable,159)
#endif
#if HAS_D3DDevice_SetRenderState_YuvEnable
COMPLEX_HOOK(D3DDevice_SetRenderState_YuvEnable,160)
#endif
#if HAS_D3DDevice_SetRenderState_OcclusionCullEnable
COMPLEX_HOOK(D3DDevice_SetRenderState_OcclusionCullEnable,161)
#endif
#if HAS_D3DDevice_SetRenderState_StencilCullEnable
COMPLEX_HOOK(D3DDevice_SetRenderState_StencilCullEnable,162)
#endif
#if HAS_D3DDevice_SetRenderState_RopZCmpAlwaysRead
COMPLEX_HOOK(D3DDevice_SetRenderState_RopZCmpAlwaysRead,163)
#endif
#if HAS_D3DDevice_SetRenderState_RopZRead
COMPLEX_HOOK(D3DDevice_SetRenderState_RopZRead,164)
#endif
#if HAS_D3DDevice_SetRenderState_DoNotCullUncompressed
COMPLEX_HOOK(D3DDevice_SetRenderState_DoNotCullUncompressed,165)
#endif
#endif
#if HAS_D3DDevice_MakeSpace
HOOK(D3DDevice_MakeSpace,hulkpocket::kDefaultMakeSpace)
#endif
#if HAS_D3D_MakeRequestedSpace
HOOK(D3D_MakeRequestedSpace,hulkpocket::kDefaultMakeSpace)
#endif

// Local-only verified original viewport writer. No guessed ABI or return:
// invoke its generated implementation unchanged, observing a copied context.
#if __has_include("hulk_verified_viewport_writer.h")
#include "hulk_verified_viewport_writer.h"
#if HULK_VERIFIED_VIEWPORT_WRITER
extern "C" void __imp__sub_0037F720(X86Context&,uint8_t*);
extern "C" __attribute__((visibility("default")))
void sub_0037F720(X86Context& ctx,uint8_t* base){
    auto saved=ctx;
    if(hulkpocket::graphics_callback){
        auto observation=saved;
        hulkpocket::graphics_callback(observation,base,hulkpocket::kViewportTraceEnter);
    }
    __imp__sub_0037F720(ctx,base);
    if(hulkpocket::graphics_callback){
        auto observation=saved;
        hulkpocket::graphics_callback(observation,base,hulkpocket::kViewportTraceLeave);
    }
}
#endif
#endif

#if __has_include("hulk_verified_block_on_time.h")
#include "hulk_verified_block_on_time.h"
#if HULK_VERIFIED_BLOCK_ON_TIME
// Verified local USA routine: original ret 8, time and wait flags on stack.
// Do not execute hardware polling or invent a register backing page.
extern "C" __attribute__((visibility("default")))
void sub_0038A2C0(X86Context& ctx,uint8_t* base){
    Boundary(ctx,base,hulkpocket::kGraphicsBlockOnTime,"D3D_BlockOnTime",true);
}
#endif
#endif

#if __has_include("hulk_verified_startup_text.h")
#include "hulk_verified_startup_text.h"
#if HULK_VERIFIED_STARTUP_TEXT
extern "C" void __imp__sub_0029B330(X86Context&,uint8_t*);
extern "C" __attribute__((visibility("default")))
void sub_0029B330(X86Context& ctx,uint8_t* base){
    if(hulkpocket::graphics_callback){
        auto observation=ctx;
        hulkpocket::graphics_callback(observation,base,hulkpocket::kStartupTextTrace);
    }
    __imp__sub_0029B330(ctx,base);
}
#endif
#endif

// Verified named XDK audio exports. Unsupported APIs stop explicitly rather
// than execute console hardware code against host-owned audio objects.
namespace {
[[maybe_unused]] void SoundBoundary(X86Context& ctx,uint8_t* base,uint32_t id,unsigned n,const char* name){
    if(!Stdcall(name)||!hulkpocket::graphics_callback)throw std::runtime_error("Audio requires checked stdcall adapter");
    const auto esp=ctx.esp;
    std::fprintf(stderr,"HULK DSOUND CALL %s ESP=%08X thread=%08X\n",name,esp,ctx.thread_handle);
    hulkpocket::graphics_callback(ctx,base,id);
    if(uint64_t(esp)+4+4*n!=ctx.esp)throw std::runtime_error("Audio adapter did not consume guest frame");
}
}
#define SOUND_HOOK(name,id,n) extern "C" __attribute__((visibility("default"))) \
void GUEST(name)(X86Context& ctx,uint8_t* base){SoundBoundary(ctx,base,hulkpocket::id,n,#name);}
#define SOUND_STOP(name) extern "C" __attribute__((visibility("default"))) \
void GUEST(name)(X86Context&,uint8_t*){throw std::runtime_error("Unsupported DirectSound boundary: " #name);}
#if HAS_DirectSoundCreate
SOUND_HOOK(DirectSoundCreate,kSoundCreate,3)
#endif
#if HAS_IDirectSound_Release
SOUND_HOOK(IDirectSound_Release,kSoundReleaseDevice,1)
#endif
#if HAS_IDirectSound_GetSpeakerConfig
SOUND_HOOK(IDirectSound_GetSpeakerConfig,kSoundSpeaker,2)
#endif
#if HAS_DirectSoundDoWork
SOUND_HOOK(DirectSoundDoWork,kSoundPump,0)
#endif
#if HAS_DirectSoundCreateBuffer
SOUND_HOOK(DirectSoundCreateBuffer,kSoundNewBuffer,2)
#endif
#if HAS_IDirectSound_CreateSoundBuffer
SOUND_HOOK(IDirectSound_CreateSoundBuffer,kSoundNewDeviceBuffer,4)
#endif
#if HAS_IDirectSoundBuffer_Release
SOUND_HOOK(IDirectSoundBuffer_Release,kSoundBufferRelease,1)
#endif
#if HAS_IDirectSoundBuffer_SetBufferData
SOUND_HOOK(IDirectSoundBuffer_SetBufferData,kSoundBufferData,3)
#endif
#if HAS_IDirectSoundBuffer_Play
SOUND_HOOK(IDirectSoundBuffer_Play,kSoundBufferPlay,4)
#endif
#if HAS_IDirectSoundBuffer_Stop
SOUND_HOOK(IDirectSoundBuffer_Stop,kSoundBufferStop,1)
#endif
#if HAS_IDirectSoundBuffer_GetStatus
SOUND_HOOK(IDirectSoundBuffer_GetStatus,kSoundBufferStatus,2)
#endif
#if HAS_IDirectSoundBuffer_SetVolume
SOUND_HOOK(IDirectSoundBuffer_SetVolume,kSoundBufferVolume,2)
#endif
#if HAS_IDirectSoundBuffer_SetFrequency
SOUND_HOOK(IDirectSoundBuffer_SetFrequency,kSoundBufferFrequency,2)
#endif
#if HAS_IDirectSoundBuffer_SetCurrentPosition
SOUND_HOOK(IDirectSoundBuffer_SetCurrentPosition,kSoundBufferSetPosition,2)
#endif
#if HAS_IDirectSoundBuffer_GetCurrentPosition
SOUND_HOOK(IDirectSoundBuffer_GetCurrentPosition,kSoundBufferGetPosition,3)
#endif
#if HAS_CDirectSoundStream_Discontinuity
SOUND_HOOK(CDirectSoundStream_Discontinuity,kSoundStreamDiscontinuity,1)
#endif
#if HAS_CDirectSoundStream_Flush
SOUND_HOOK(CDirectSoundStream_Flush,kSoundStreamFlush,1)
#endif
#if HAS_CDirectSoundStream_FlushEx
SOUND_HOOK(CDirectSoundStream_FlushEx,kSoundStreamFlushEx,4)
#endif
#if HAS_CDirectSoundStream_GetInfo
SOUND_HOOK(CDirectSoundStream_GetInfo,kSoundStreamInfo,2)
#endif
#if HAS_CDirectSoundStream_GetStatus
SOUND_HOOK(CDirectSoundStream_GetStatus,kSoundStreamStatus,2)
#endif
#if HAS_CDirectSoundStream_Pause
SOUND_HOOK(CDirectSoundStream_Pause,kSoundStreamPause,2)
#endif
#if HAS_CDirectSoundStream_Process
SOUND_HOOK(CDirectSoundStream_Process,kSoundStreamProcess,3)
#endif
#if HAS_CDirectSoundStream_SetFilter
SOUND_STOP(CDirectSoundStream_SetFilter)
#endif
#if HAS_CDirectSoundStream_SetFormat
SOUND_HOOK(CDirectSoundStream_SetFormat,kSoundStreamFormat,2)
#endif
#if HAS_CDirectSoundStream_SetFrequency
SOUND_HOOK(CDirectSoundStream_SetFrequency,kSoundStreamFrequency,2)
#endif
#if HAS_CDirectSoundStream_SetHeadroom
SOUND_HOOK(CDirectSoundStream_SetHeadroom,kSoundStreamHeadroom,2)
#endif
#if HAS_CDirectSoundStream_SetI3DL2Source
SOUND_HOOK(CDirectSoundStream_SetI3DL2Source,kSoundStreamI3dl2Source,3)
#endif
#if HAS_CDirectSoundStream_SetMaxDistance
SOUND_HOOK(CDirectSoundStream_SetMaxDistance,kSoundStreamMaxDistance,3)
#endif
#if HAS_CDirectSoundStream_SetMinDistance
SOUND_HOOK(CDirectSoundStream_SetMinDistance,kSoundStreamMinDistance,3)
#endif
#if HAS_CDirectSoundStream_SetMixBins
SOUND_HOOK(CDirectSoundStream_SetMixBins,kSoundStreamMixBins,2)
#endif
#if HAS_CDirectSoundStream_SetMixBinVolumes
SOUND_HOOK(CDirectSoundStream_SetMixBinVolumes,kSoundStreamMixBinVolumes,2)
#endif
#if HAS_CDirectSoundStream_SetPosition
SOUND_HOOK(CDirectSoundStream_SetPosition,kSoundStreamSpatialPosition,5)
#endif
#if HAS_CDirectSoundStream_SetVelocity
SOUND_HOOK(CDirectSoundStream_SetVelocity,kSoundStreamVelocity,5)
#endif
#if HAS_CDirectSoundStream_SetVolume
SOUND_HOOK(CDirectSoundStream_SetVolume,kSoundStreamVolume,2)
#endif
#if HAS_DSound_StreamVtable && HAS_DSound_StreamVtable2
#define STREAM_CREATE(name,id,n) extern "C" __attribute__((visibility("default"))) \
void GUEST(name)(X86Context& ctx,uint8_t* base){ \
    ctx.ecx=GUEST_VA(SYM_DSound_StreamVtable);ctx.edx=GUEST_VA(SYM_DSound_StreamVtable2); \
    SoundBoundary(ctx,base,hulkpocket::id,n,#name); \
}
#else
#define STREAM_CREATE(name,id,n) SOUND_STOP(name)
#endif
#if HAS_DirectSoundCreateStream
STREAM_CREATE(DirectSoundCreateStream,kSoundNewStream,2)
#endif
#if HAS_IDirectSound_CreateSoundStream
STREAM_CREATE(IDirectSound_CreateSoundStream,kSoundNewDeviceStream,4)
#endif
#if HAS_CDirectSoundStream_AddRef
SOUND_HOOK(CDirectSoundStream_AddRef,kSoundStreamAddRef,1)
#endif
#if HAS_CDirectSoundStream_Release
SOUND_HOOK(CDirectSoundStream_Release,kSoundStreamRelease,1)
#endif
#undef STREAM_CREATE
#if HAS_DirectSoundUseFullHRTF
SOUND_HOOK(DirectSoundUseFullHRTF,kSoundFullHrtf,0)
#endif
#if HAS_IDirectSound_CommitDeferredSettings
SOUND_HOOK(IDirectSound_CommitDeferredSettings,kSoundCommit3d,1)
#endif
#if HAS_IDirectSound_CommitEffectData
SOUND_HOOK(IDirectSound_CommitEffectData,kSoundCommitEffectData,1)
#endif
#if HAS_IDirectSound_DownloadEffectsImage
SOUND_HOOK(IDirectSound_DownloadEffectsImage,kSoundDownloadEffects,5)
#endif
#if HAS_IDirectSound_EnableHeadphones
SOUND_STOP(IDirectSound_EnableHeadphones)
#endif
#if HAS_IDirectSound_GetEffectData
SOUND_HOOK(IDirectSound_GetEffectData,kSoundGetEffectData,5)
#endif
#if HAS_IDirectSound_GetOutputLevels
SOUND_STOP(IDirectSound_GetOutputLevels)
#endif
#if HAS_IDirectSound_SetDistanceFactor
SOUND_HOOK(IDirectSound_SetDistanceFactor,kSoundListenerDistance,3)
#endif
#if HAS_IDirectSound_SetDopplerFactor
SOUND_HOOK(IDirectSound_SetDopplerFactor,kSoundListenerDoppler,3)
#endif
#if HAS_IDirectSound_SetEffectData
SOUND_HOOK(IDirectSound_SetEffectData,kSoundSetEffectData,6)
#endif
#if HAS_IDirectSound_SetI3DL2Listener
SOUND_HOOK(IDirectSound_SetI3DL2Listener,kSoundI3dl2Listener,3)
#endif
#if HAS_IDirectSound_SetMixBinHeadroom
SOUND_STOP(IDirectSound_SetMixBinHeadroom)
#endif
#if HAS_IDirectSound_SetOrientation
SOUND_HOOK(IDirectSound_SetOrientation,kSoundListenerOrientation,8)
#endif
#if HAS_IDirectSound_SetPosition
SOUND_HOOK(IDirectSound_SetPosition,kSoundListenerPosition,5)
#endif
#if HAS_IDirectSound_SetRolloffFactor
SOUND_HOOK(IDirectSound_SetRolloffFactor,kSoundListenerRolloff,3)
#endif
#if HAS_IDirectSound_SetVelocity
SOUND_HOOK(IDirectSound_SetVelocity,kSoundListenerVelocity,5)
#endif
#if HAS_IDirectSound_SynchPlayback
SOUND_HOOK(IDirectSound_SynchPlayback,kSoundSynchPlayback,1)
#endif
#if HAS_IDirectSoundBuffer_Lock
SOUND_HOOK(IDirectSoundBuffer_Lock,kSoundBufferLock,8)
#endif
#if HAS_IDirectSoundBuffer_Pause
SOUND_HOOK(IDirectSoundBuffer_Pause,kSoundBufferPause,2)
#endif
#if HAS_IDirectSoundBuffer_SetAllParameters
SOUND_HOOK(IDirectSoundBuffer_SetAllParameters,kSoundAll3d,3)
#endif
#if HAS_IDirectSoundBuffer_SetEG
SOUND_STOP(IDirectSoundBuffer_SetEG)
#endif
#if HAS_IDirectSoundBuffer_SetFilter
SOUND_STOP(IDirectSoundBuffer_SetFilter)
#endif
#if HAS_IDirectSoundBuffer_SetFormat
SOUND_HOOK(IDirectSoundBuffer_SetFormat,kSoundBufferFormat,2)
#endif
#if HAS_IDirectSoundBuffer_SetHeadroom
SOUND_HOOK(IDirectSoundBuffer_SetHeadroom,kSoundBufferHeadroom,2)
#endif
#if HAS_IDirectSoundBuffer_SetI3DL2Source
SOUND_HOOK(IDirectSoundBuffer_SetI3DL2Source,kSoundBufferI3dl2Source,3)
#endif
#if HAS_IDirectSoundBuffer_SetLoopRegion
SOUND_HOOK(IDirectSoundBuffer_SetLoopRegion,kSoundBufferLoopRegion,3)
#endif
#if HAS_IDirectSoundBuffer_SetMaxDistance
SOUND_HOOK(IDirectSoundBuffer_SetMaxDistance,kSoundBufferMaxDistance,3)
#endif
#if HAS_IDirectSoundBuffer_SetMinDistance
SOUND_HOOK(IDirectSoundBuffer_SetMinDistance,kSoundBufferMinDistance,3)
#endif
#if HAS_IDirectSoundBuffer_SetMixBins
SOUND_HOOK(IDirectSoundBuffer_SetMixBins,kSoundBufferMixBins,2)
#endif
#if HAS_IDirectSoundBuffer_SetMixBinVolumes
SOUND_HOOK(IDirectSoundBuffer_SetMixBinVolumes,kSoundBufferMixBinVolumes,2)
#endif
#if HAS_IDirectSoundBuffer_SetOutputBuffer
SOUND_STOP(IDirectSoundBuffer_SetOutputBuffer)
#endif
#if HAS_IDirectSoundBuffer_SetPlayRegion
SOUND_HOOK(IDirectSoundBuffer_SetPlayRegion,kSoundBufferPlayRegion,3)
#endif
#if HAS_IDirectSoundBuffer_SetPosition
SOUND_HOOK(IDirectSoundBuffer_SetPosition,kSoundBufferSpatialPosition,5)
#endif
#if HAS_IDirectSoundBuffer_SetRolloffCurve
SOUND_HOOK(IDirectSoundBuffer_SetRolloffCurve,kSoundRolloffCurve,4)
#endif
#if HAS_IDirectSoundBuffer_SetVelocity
SOUND_HOOK(IDirectSoundBuffer_SetVelocity,kSoundBufferVelocity,5)
#endif
#if HAS_IDirectSoundBuffer_StopEx
SOUND_HOOK(IDirectSoundBuffer_StopEx,kSoundStopEx,4)
#endif
#if HAS_IDirectSoundBuffer_Unlock
SOUND_HOOK(IDirectSoundBuffer_Unlock,kSoundBufferUnlock,5)
#endif
#if HAS_IDirectSoundStream_SetEG
SOUND_STOP(IDirectSoundStream_SetEG)
#endif
#if HAS_XAudioDownloadEffectsImage
SOUND_STOP(XAudioDownloadEffectsImage)
#endif
