#pragma once
#include "cpu/x86_context.h"
#include <cstdint>
namespace hulkpocket {
// Internal host callback IDs, never guest kernel ordinals.
constexpr uint32_t kGraphicsCreate=0x10000,kGraphicsDraw=0x10001,
    kGraphicsDrawIndexed=0x10002,kGraphicsSwap=0x10003,
    kXapiLaunchImage=0x10004,kHulkAllocatorStop=0x10005,kMissingGuestEntry=0x10006,
    kXapiInitDevices=0x10007,kXapiInputOpen=0x10008,kXapiInputClose=0x10009,
    kXapiInputGetState=0x1000a,kXapiInputSetState=0x1000b,kXapiInputGetCapabilities=0x1000c,kGraphicsClear=0x1000d,kViewportTraceEnter=0x1000e,kViewportTraceLeave=0x1000f,kGraphicsBlockOnTime=0x10010,kGraphicsVBlank=0x10011,kStartupTextTrace=0x10012;
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
