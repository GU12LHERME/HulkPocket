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
[[maybe_unused,noreturn]] void Boundary(X86Context& ctx,uint8_t* base,uint32_t id,const char* name,bool stack_abi){
    std::fprintf(stderr,"D3D8 GUEST BOUNDARY: %s ESP=%08X EAX=%08X ECX=%08X EDX=%08X thread=%08X\n",
        name,ctx.esp,ctx.eax,ctx.ecx,ctx.edx,ctx.thread_handle);
    if(!stack_abi)throw std::runtime_error("D3D8 boundary has explicit ABI mapping; host adapter required");
    if(hulkpocket::graphics_callback)hulkpocket::graphics_callback(ctx,base,id);
    throw std::runtime_error("D3D8 guest device integration checkpoint");
}
}
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#ifdef IMAGE_NAMED
#define GUEST(name) CAT(sub_,name)
#else
#define GUEST(name) CAT(sub_,SYM_##name)
#endif
#define HOOK(name,id) extern "C" __attribute__((visibility("default"))) \
    void GUEST(name)(X86Context& ctx,uint8_t* base){Boundary(ctx,base,id,#name,Stdcall(#name));}
#if HAS_Direct3D_CreateDevice
HOOK(Direct3D_CreateDevice,hulkpocket::kGraphicsCreate)
#else
#error "Game has no generated Direct3D_CreateDevice symbol"
#endif
#if HAS_D3DDevice_DrawVertices
HOOK(D3DDevice_DrawVertices,hulkpocket::kGraphicsDraw)
#endif
#if HAS_D3DDevice_DrawIndexedVertices
HOOK(D3DDevice_DrawIndexedVertices,hulkpocket::kGraphicsDrawIndexed)
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
