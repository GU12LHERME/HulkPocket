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
    // Only CreateDevice has a checked diagnostic continuation. A callback
    // that did not complete its stdcall frame must still stop execution.
    if((id==hulkpocket::kGraphicsCreate||id==hulkpocket::kGraphicsClear)&&
        uint64_t(original_esp)+28==ctx.esp&&ctx.eax==0)return;
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
