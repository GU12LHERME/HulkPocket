#pragma once
#include "cpu/x86_context.h"
#include <cstdint>
namespace hulkpocket {
// Internal host callback IDs, never guest kernel ordinals.
constexpr uint32_t kGraphicsCreate=0x10000,kGraphicsDraw=0x10001,
    kGraphicsDrawIndexed=0x10002,kGraphicsSwap=0x10003,
    kXapiLaunchImage=0x10004,kHulkAllocatorStop=0x10005,kMissingGuestEntry=0x10006,
    kXapiInitDevices=0x10007,kXapiInputOpen=0x10008,kXapiInputClose=0x10009,
    kXapiInputGetState=0x1000a,kXapiInputSetState=0x1000b,kXapiInputGetCapabilities=0x1000c;
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
