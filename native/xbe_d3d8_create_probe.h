#pragma once
#include "xbe_d3d8_bridge.h"
#include "xbe_d3d8_device_memory.h"
#include "xbe_gles_backend.h"
#include <cstdio>
namespace hulkpocket {
// Actual CreateDevice arguments and generated global addresses drive the
// previously isolated GLES implementation. Run and destroy on the guest
// caller thread. A diagnostic clear is NOT a frame drawn by the game.
inline void RunD3dCreateProbe(XboxGuestAddressSpace& mem,XbeKernelPool& pool,
    XbePhysicalMemory& physical,uint32_t device,uint32_t slot,uint32_t pp){
    XbeD3d8DeviceMemory buffers(mem,pool,physical);
    buffers.Initialize(device,slot,pp);
    auto w=mem.Read32(device+0xee8),h=mem.Read32(device+0xeec);
    XbeGlesBackend backend;
    XbeD3d8Bridge bridge(mem,backend);
    bridge.Initialize(w,h);
    bridge.BindInitialTargets(buffers.BackBuffer(0),buffers.Depth());
    auto state=bridge.Snapshot();
    gpu::ScopedGuestMemory scope(mem);
    backend.Clear(state,0,nullptr,0xf0u|(buffers.Depth()?gpu::kClearZ:0u),0xff123456,1.0f,0);
    auto pixel=backend.ReadPixel(w/2,h/2);
    if(pixel!=std::array<uint8_t,4>{0x12,0x34,0x56,0xff})
        throw std::runtime_error("D3D device diagnostic readback mismatch");
    bridge.Present(buffers.BackBuffer(0));
    if(buffers.Rotate()!=1)throw std::runtime_error("D3D device diagnostic swap count mismatch");
    std::fprintf(stderr,"D3D8 LIVE DEVICE PROBE PASS: %ux%u color=%08X depth=%08X RGBA=%02X%02X%02X%02X\n",
        w,h,state.rt.header,state.zs.header,unsigned(pixel[0]),unsigned(pixel[1]),unsigned(pixel[2]),unsigned(pixel[3]));
}
}
