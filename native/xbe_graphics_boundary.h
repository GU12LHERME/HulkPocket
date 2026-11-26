#pragma once
#include "cpu/x86_context.h"
#include <cstdint>
namespace hulkpocket {
// Internal host callback IDs, never guest kernel ordinals.
constexpr uint32_t kGraphicsCreate=0x10000,kGraphicsDraw=0x10001,
    kGraphicsDrawIndexed=0x10002,kGraphicsSwap=0x10003;
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
