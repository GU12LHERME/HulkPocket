#include "xbe_gles_backend.h"
#include "xbe_d3d8_bridge.h"
#include <cassert>
#include <cstdio>
using namespace hulkpocket;
int main(){
    XboxGuestAddressSpace mem;mem.Commit(0x10000,0x10000);mem.Commit(0x80100000,0x10000);
    XbeGlesBackend backend;XbeD3d8Bridge bridge(mem,backend);bridge.Initialize(64,64);
    X86Context cpu{};
    auto call=[&](std::string_view name,std::initializer_list<uint32_t> args){
        cpu.esp=0x1f000;mem.Write32(cpu.esp,0x20000);
        unsigned i=0;for(auto v:args)mem.Write32(cpu.esp+4+4*i++,v);
        bridge.Call(name,cpu,D3dAbi::Stdcall(unsigned(args.size())));
        assert(cpu.esp==0x1f004+4*args.size());
    };
    mem.Write32(0x10100,0x50000);mem.Write32(0x10104,0x100000);
    mem.Write32(0x1010c,0x00011221);mem.Write32(0x10110,63|(63<<12)|(3<<24));
    call("D3DDevice_SetRenderTarget",{0x10100,0});
    call("D3DDevice_Clear",{0,0,0xf0,0xff00ff00,std::bit_cast<uint32_t>(1.0f),0});
    assert((backend.ReadPixel(0,0)==std::array<uint8_t,4>{0,255,0,255}));
    struct V{float x,y,z,rhw;uint32_t color;};
    const V v[]={{8,8,0.5,1,0xffff0000},{56,8,0.5,1,0xffff0000},{32,56,0.5,1,0xffff0000}};
    mem.Write(0x80105000,v,sizeof(v));mem.Write32(0x10204,0x105000);
    call("D3DDevice_SetStreamSource",{0,0x10200,sizeof(V)});
    call("D3DDevice_SetVertexShader",{0x44});
    call("D3DDevice_DrawVertices",{5,0,3});
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{255,0,0,255}));
    assert((backend.ReadPixel(1,1)==std::array<uint8_t,4>{0,255,0,255}));
    call("D3DDevice_Clear",{0,0,0xf0,0xff0000ff,std::bit_cast<uint32_t>(1.0f),0});
    const uint16_t ix[]={0,1,2};mem.Write(0x10300,ix,sizeof(ix));
    call("D3DDevice_DrawIndexedVertices",{5,3,0x10300});
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{255,0,0,255}));
    assert((backend.ReadPixel(1,1)==std::array<uint8_t,4>{0,0,255,255}));
    auto fence=backend.Fence();assert(!backend.FencePending(fence));backend.WaitFence(fence);
    assert(bridge.Present(0x10100)==1);
    std::puts("GLES real clear, triangle, indexed triangle, pixel readback, fence and pbuffer present passed");
}
