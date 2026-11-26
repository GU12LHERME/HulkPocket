#include "xbe_gles_backend.h"
#include "xbe_d3d8_bridge.h"
#include "xbe_bc_decode.h"
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
    // Real guest texture: 2x2 swizzled A8R8G8B8, red/green/blue/white.
    mem.Write32(0x10400,0x40000);mem.Write32(0x10404,0x106000);
    mem.Write32(0x1040c,0x01110621);mem.Write32(0x10410,0);
    const uint32_t tex[]={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff};
    mem.Write(0x80106000,tex,sizeof(tex));
    struct TV{float x,y,z,rhw;uint32_t color;float u,v;};
    const TV tv[]={{0,0,.5,1,0xffffffff,0,0},{64,0,.5,1,0xffffffff,1,0},{64,64,.5,1,0xffffffff,1,1},
                   {0,0,.5,1,0xffffffff,0,0},{64,64,.5,1,0xffffffff,1,1},{0,64,.5,1,0xffffffff,0,1}};
    mem.Write(0x80105000,tv,sizeof(tv));
    call("D3DDevice_SetStreamSource",{0,0x10200,sizeof(TV)});
    call("D3DDevice_SetVertexShader",{0x144});
    call("D3DDevice_SetTexture",{0,0x10400});
    auto ts=[&](uint32_t k,uint32_t value){call("D3DDevice_SetTextureStageStateNotInline",{0,k,value});};
    ts(gpu::kTssAddressU,3);ts(gpu::kTssAddressV,3);
    ts(gpu::kTssMinFilter,1);ts(gpu::kTssMagFilter,1);
    ts(gpu::kTssColorOp,4);ts(gpu::kTssColorArg1,2);
    ts(gpu::kTssAlphaOp,4);ts(gpu::kTssAlphaArg1,2);
    call("D3DDevice_DrawVertices",{5,0,6});
    assert((backend.ReadPixel(8,8)==std::array<uint8_t,4>{255,0,0,255}));
    assert((backend.ReadPixel(56,8)==std::array<uint8_t,4>{0,255,0,255}));
    assert((backend.ReadPixel(8,56)==std::array<uint8_t,4>{0,0,255,255}));
    assert((backend.ReadPixel(56,56)==std::array<uint8_t,4>{255,255,255,255}));
    // Rewrite the same texture address, no stale cached pixels.
    mem.Write32(0x80106000,0xffffff00);
    call("D3DDevice_DrawVertices",{5,0,6});
    assert((backend.ReadPixel(8,8)==std::array<uint8_t,4>{255,255,0,255}));
    ts(gpu::kTssMinFilter,2);ts(gpu::kTssMagFilter,2);
    call("D3DDevice_DrawVertices",{5,0,6});
    auto center=backend.ReadPixel(32,32);assert(center[0]>80&&center[0]<200&&center[1]>80&&center[1]<200&&center[2]>80&&center[2]<200);
    // DXT1 solid red block, then DXT3/5 known alpha vectors.
    const std::vector<uint8_t> bc1={0,0xf8,0,0,0,0,0,0};
    mem.Write32(0x1040c,0x02210c21);mem.Write(0x80106000,bc1.data(),bc1.size());
    call("D3DDevice_DrawVertices",{5,0,6});
    assert((backend.ReadPixel(32,32)==std::array<uint8_t,4>{255,0,0,255}));
    std::vector<uint8_t> bc2(16,0);std::fill_n(bc2.begin(),8,0x88);std::copy(bc1.begin(),bc1.end(),bc2.begin()+8);
    auto decoded=DecodeXboxBc(3,3,gpu::Host::BC2,bc2);
    assert(decoded.size()==36&&decoded[0]==255&&decoded[3]==136&&decoded[35]==136);
    std::vector<uint8_t> bc3(16,0);bc3[0]=200;bc3[1]=100;std::copy(bc1.begin(),bc1.end(),bc3.begin()+8);
    decoded=DecodeXboxBc(4,4,gpu::Host::BC3,bc3);assert(decoded[0]==255&&decoded[3]==200);
    auto fence=backend.Fence();assert(!backend.FencePending(fence));backend.WaitFence(fence);
    assert(bridge.Present(0x10100)==1);
    std::puts("GLES real clear, triangle, indexed triangle, pixel readback, Xbox texture upload, filtering, DXT fallback, fence and pbuffer present passed");
}
