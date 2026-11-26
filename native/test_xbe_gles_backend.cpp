#include "xbe_gles_backend.h"
#include "xbe_d3d8_bridge.h"
#include "xbe_d3d8_create_probe.h"
#include "xbe_bc_decode.h"
#include <cassert>
#include <cstdio>
using namespace hulkpocket;
int main(){
    {
        XboxGuestAddressSpace guest;guest.Commit(0x20000,0x10000);
        XbeKernelPool pool(guest,0x09000000,0x10000);
        XbePhysicalMemory phys(guest);
        constexpr uint32_t dev=0x20000,slot=0x23000,pp=0x23100;
        guest.Write32(pp,64);guest.Write32(pp+4,64);guest.Write32(pp+8,6);
        guest.Write32(pp+12,1);guest.Write32(pp+0x20,1);guest.Write32(pp+0x24,0x2a);
        RunD3dCreateProbe(guest,pool,phys,dev,slot,pp);
        assert(pool.Outstanding()==0&&phys.AllocatedPages()==0&&guest.Read32(slot)==0);
    }
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
    {
        gpu::ScopedGuestMemory scoped(mem);
        backend.ReadbackColor(bridge.Snapshot().rt);
        assert(mem.Read32(0x80100000+(24*256+32*4))==0xffff0000);
        assert(mem.Read32(0x80100000+(1*256+1*4))==0xff00ff00);
    }
    call("D3DDevice_Clear",{0,0,0xf0,0xff0000ff,std::bit_cast<uint32_t>(1.0f),0});
    const uint16_t ix[]={0,1,2};mem.Write(0x10300,ix,sizeof(ix));
    call("D3DDevice_DrawIndexedVertices",{5,3,0x10300});
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{255,0,0,255}));
    assert((backend.ReadPixel(1,1)==std::array<uint8_t,4>{0,0,255,255}));
    // First real Hulk draw requests primitive 2 (line list).
    // Verify actual GLES line coverage, allowing the rasterization edge rule.
    const V line[]={{8,32,.5,1,0xffffff00},{56,32,.5,1,0xffffff00}};
    mem.Write(0x80105000,line,sizeof(line));
    call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.0f),0});
    call("D3DDevice_DrawVertices",{2,0,2});
    bool line_pixel=false;
    for(unsigned y=30;y<=33;++y)
        if(backend.ReadPixel(32,y)==std::array<uint8_t,4>{255,255,0,255})line_pixel=true;
    assert(line_pixel);
    assert((backend.ReadPixel(32,20)==std::array<uint8_t,4>{0,0,0,255}));
    // Verify the restricted diagnostic replay uses real XYZ diffuse input
    // and does not repair guest viewport state or consume the raw guard.
    {
        constexpr uint32_t dev=0x16000,shader=0x19000;
        mem.Write32(dev+0x794,shader);mem.Write32(shader+4,0x400);
        for(unsigned i=0;i<16;++i)mem.Write32(shader+0x14+16*i+8,2);
        mem.Write32(shader+0x14+8,0x32);
        mem.Write32(shader+0x14+3*16+4,12);mem.Write32(shader+0x14+3*16+8,0x40);
        mem.Write32(dev+0xef4,0x3f800000);
        mem.Write32(dev+0xef8,0x3f080000);mem.Write32(dev+0xefc,0x3f080000);
        for(unsigned tr:{0u,1u,6u})for(unsigned i=0;i<4;++i)
            mem.Write32(dev+0xc60+64*tr+20*i,0x3f800000);
        struct XYZ{float x,y,z;uint32_t color;};
        const XYZ xyz[]={{-.75f,0,.5f,0xffff00ff},{.75f,0,.5f,0xffff00ff}};
        mem.Write(0x80105000,xyz,sizeof(xyz));
        call("D3DDevice_SetStreamSource",{0,0x10200,sizeof(XYZ)});
        assert(bridge.SyncFixedDiffuseShader(dev)==0x42);
        call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.0f),0});
        bridge.ReplayFixedLinesDiagnostic(0,2);
        bool replay_pixel=false;
        for(unsigned y=30;y<=33;++y)
            if(backend.ReadPixel(32,y)==std::array<uint8_t,4>{255,0,255,255})replay_pixel=true;
        assert(replay_pixel&&bridge.Snapshot().viewport.w==0&&mem.Read32(dev+0xee8)==0);
        mem.Write32(0x10340,0);mem.Write32(0x10344,0);
        mem.Write32(0x10348,64);mem.Write32(0x1034c,64);
        mem.Write32(0x10350,0);mem.Write32(0x10354,0x3f800000);
        call("D3DDevice_SetViewport",{0x10340});
        call("D3DDevice_SetStreamSource",{0,0x10200,sizeof(V)});
        call("D3DDevice_SetVertexShader",{0x44});
    }
    // Quad conversion and native strips exercise the Xbox primitive assembler.
    const V quad[]={{8,8,.5,1,0xff00ff00},{56,8,.5,1,0xff00ff00},
                    {56,56,.5,1,0xff00ff00},{8,56,.5,1,0xff00ff00}};
    mem.Write(0x80105000,quad,sizeof(quad));
    call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.0f),0});
    call("D3DDevice_DrawVertices",{8,0,4});
    assert((backend.ReadPixel(32,32)==std::array<uint8_t,4>{0,255,0,255}));
    const V strip[]={quad[0],quad[1],quad[3],quad[2]};
    mem.Write(0x80105000,strip,sizeof(strip));
    call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.0f),0});
    call("D3DDevice_DrawVertices",{6,0,4});
    assert((backend.ReadPixel(32,32)==std::array<uint8_t,4>{0,255,0,255}));
    // Xbox top-left rectangle coordinates must become GLES bottom-left scissor.
    const int32_t rect[]={4,8,20,24};mem.Write(0x10320,rect,sizeof(rect));
    call("D3DDevice_Clear",{1,0x10320,0xf0,0xffff0000,std::bit_cast<uint32_t>(1.0f),0});
    assert((backend.ReadPixel(5,9)==std::array<uint8_t,4>{255,0,0,255}));
    assert((backend.ReadPixel(5,54)==std::array<uint8_t,4>{0,0,0,255}));
    assert((backend.ReadPixel(32,32)==std::array<uint8_t,4>{0,255,0,255}));
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
    // Disable the texture combiner and bind a matching 64x64 D24S8 surface.
    call("D3DDevice_SetTexture",{0,0});ts(gpu::kTssColorOp,0);ts(gpu::kTssAlphaOp,0);
    mem.Write32(0x10500,0x50000);mem.Write32(0x10504,0x108000);
    mem.Write32(0x1050c,0x00012e21);mem.Write32(0x10510,63|(63<<12)|(3<<24));
    call("D3DDevice_SetRenderTarget",{0x10100,0x10500});
    call("D3DDevice_SetVertexShader",{0x44});
    call("D3DDevice_SetStreamSource",{0,0x10200,sizeof(V)});
    auto rs=[&](uint32_t method,uint32_t value){call("D3DDevice_SetRenderState_Simple",{method,value});};
    call("D3DDevice_SetRenderState_ZEnable",{1});rs(gpu::kMZFunc,0x201);rs(gpu::kMZWriteEnable,1);
    call("D3DDevice_Clear",{0,0,0xf1,0xff000000,std::bit_cast<uint32_t>(1.0f),0});
    auto tri=[&](float z,uint32_t color){
        const V p[]={{8,8,z,1,color},{56,8,z,1,color},{32,56,z,1,color}};
        mem.Write(0x80105000,p,sizeof(p));call("D3DDevice_DrawVertices",{5,0,3});
    };
    tri(.2f,0xffff0000);tri(.8f,0xff0000ff);
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{255,0,0,255}));
    tri(.1f,0xff00ff00);
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{0,255,0,255}));
    // Depth writes off: a closer draw does not prevent a later middle-depth draw.
    rs(gpu::kMZWriteEnable,0);tri(.02f,0xff0000ff);tri(.05f,0xffff0000);
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{255,0,0,255}));
    // Source alpha / inverse source alpha, 50% red on blue.
    call("D3DDevice_SetRenderState_ZEnable",{0});
    call("D3DDevice_Clear",{0,0,0xf0,0xff0000ff,std::bit_cast<uint32_t>(1.0f),0});
    rs(gpu::kMBlendEnable,1);rs(gpu::kMSrcBlend,0x302);rs(gpu::kMDestBlend,0x303);rs(gpu::kMBlendOp,0x8006);
    tri(.5f,0x80ff0000);
    auto mixed=backend.ReadPixel(32,24);assert(mixed[0]>=127&&mixed[0]<=129&&mixed[1]==0&&mixed[2]>=126&&mixed[2]<=128);
    rs(gpu::kMBlendEnable,0);
    // Alpha test: low alpha is discarded, high alpha reaches the target.
    call("D3DDevice_Clear",{0,0,0xf0,0xff0000ff,std::bit_cast<uint32_t>(1.0f),0});
    rs(gpu::kMAlphaTestEnable,1);rs(gpu::kMAlphaFunc,0x204);rs(gpu::kMAlphaRef,127);
    tri(.5f,0x40ff0000);
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{0,0,255,255}));
    tri(.5f,0xc000ff00);
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{0,255,0,192}));
    rs(gpu::kMAlphaTestEnable,0);
    // Real stencil write and test: masked color draw writes stencil, then
    // an equal test permits green only in the marked triangle.
    call("D3DDevice_Clear",{0,0,gpu::kClearTarget|gpu::kClearStencil,0xff0000ff,std::bit_cast<uint32_t>(1.0f),0});
    call("D3DDevice_SetRenderState_StencilEnable",{1});
    call("D3DDevice_SetRenderState_StencilFail",{GL_KEEP});
    rs(gpu::kMStencilFunc,GL_ALWAYS);rs(gpu::kMStencilRef,7);
    rs(gpu::kMStencilMask,0xff);rs(gpu::kMStencilWriteMask,0xff);
    rs(gpu::kMStencilZFail,GL_KEEP);rs(gpu::kMStencilPass,GL_REPLACE);
    rs(gpu::kMColorWriteEnable,0);tri(.5f,0xffff0000);
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{0,0,255,255}));
    rs(gpu::kMColorWriteEnable,0x01010101);
    rs(gpu::kMStencilFunc,GL_EQUAL);rs(gpu::kMStencilPass,GL_KEEP);
    tri(.5f,0xff00ff00);
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{0,255,0,255}));
    rs(gpu::kMStencilRef,3);tri(.5f,0xffff0000);
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{0,255,0,255}));
    // Stencil clear ignores the previous stencil write mask.
    rs(gpu::kMStencilWriteMask,0);
    call("D3DDevice_Clear",{0,0,gpu::kClearStencil,0,std::bit_cast<uint32_t>(1.0f),3});
    tri(.5f,0xffff0000);
    assert((backend.ReadPixel(32,24)==std::array<uint8_t,4>{255,0,0,255}));
    call("D3DDevice_SetRenderState_StencilEnable",{0});
    // Restore the previous blend sample for the existing channel-mask test.
    call("D3DDevice_Clear",{0,0,0xf0,0xff0000ff,std::bit_cast<uint32_t>(1.0f),0});
    rs(gpu::kMBlendEnable,1);tri(.5f,0x80ff0000);rs(gpu::kMBlendEnable,0);
    // ARGB channel mask: write only green; preserve previous red/blue/alpha.
    rs(gpu::kMColorWriteEnable,0x00000100);
    tri(.5f,0xff00ff00);
    auto masked=backend.ReadPixel(32,24);assert(masked[0]==mixed[0]&&masked[1]==255&&masked[2]==mixed[2]&&masked[3]==mixed[3]);
    rs(gpu::kMColorWriteEnable,0x01010101);
    // World-space XYZ with nonidentity world translation and view scaling.
    call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.0f),0});
    struct WV{float x,y,z;uint32_t color;};
    const WV wv[]={{-.9f,-.5f,.5f,0xffffff00},{-.1f,-.5f,.5f,0xffffff00},{-.5f,.5f,.5f,0xffffff00}};
    mem.Write(0x80105000,wv,sizeof(wv));call("D3DDevice_SetStreamSource",{0,0x10200,sizeof(WV)});
    call("D3DDevice_SetVertexShader",{0x42});
    const float world[]={1,0,0,0,0,1,0,0,0,0,1,0,1,0,0,1};
    const float view[]={.8f,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    mem.Write(0x10600,world,sizeof(world));mem.Write(0x10640,view,sizeof(view));
    call("D3DDevice_SetTransform",{6,0x10600});call("D3DDevice_SetTransform",{0,0x10640});
    call("D3DDevice_DrawVertices",{5,0,3});
    assert((backend.ReadPixel(44,32)==std::array<uint8_t,4>{255,255,0,255}));
    assert((backend.ReadPixel(16,32)==std::array<uint8_t,4>{0,0,0,255}));
    // XYZ viewport in Xbox top-left coordinates: check that the actual
    // rasterization is confined to the selected region, not the full target.
    const uint32_t partial[]={8,12,32,24,std::bit_cast<uint32_t>(.25f),std::bit_cast<uint32_t>(.75f)};
    mem.Write(0x10340,partial,sizeof(partial));
    call("D3DDevice_SetViewport",{0x10340});
    const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    mem.Write(0x10600,identity,sizeof(identity));
    call("D3DDevice_SetTransform",{6,0x10600});call("D3DDevice_SetTransform",{0,0x10600});
    const WV full[]={{-1,-1,.5f,0xffff0000},{1,-1,.5f,0xffff0000},{1,1,.5f,0xffff0000},
                     {-1,-1,.5f,0xffff0000},{1,1,.5f,0xffff0000},{-1,1,.5f,0xffff0000}};
    mem.Write(0x80105000,full,sizeof(full));
    call("D3DDevice_SetRenderState_ZEnable",{1});rs(gpu::kMZFunc,GL_LESS);rs(gpu::kMZWriteEnable,1);
    call("D3DDevice_Clear",{0,0,0xf1,0xff000000,std::bit_cast<uint32_t>(.6f),0});
    call("D3DDevice_DrawVertices",{5,0,6});
    assert((backend.ReadPixel(24,24)==std::array<uint8_t,4>{255,0,0,255}));
    assert((backend.ReadPixel(24,44)==std::array<uint8_t,4>{0,0,0,255}));
    assert((backend.ReadPixel(4,24)==std::array<uint8_t,4>{0,0,0,255}));
    // A Z=.5 vertex maps to .5 with [.25,.75], and .8 with [.6,1].
    mem.Write32(0x10350,std::bit_cast<uint32_t>(.6f));mem.Write32(0x10354,std::bit_cast<uint32_t>(1.0f));
    call("D3DDevice_SetViewport",{0x10340});
    call("D3DDevice_Clear",{0,0,0xf1,0xff000000,std::bit_cast<uint32_t>(.6f),0});
    call("D3DDevice_DrawVertices",{5,0,6});
    assert((backend.ReadPixel(24,24)==std::array<uint8_t,4>{0,0,0,255}));
    mem.Write32(0x10348,64);call("D3DDevice_SetViewport",{0x10340});
    bool outside=false;
    try{call("D3DDevice_DrawVertices",{5,0,6});}catch(const std::invalid_argument&){outside=true;}
    assert(outside&&cpu.esp==0x1f000);
    auto fence=backend.Fence();assert(!backend.FencePending(fence));backend.WaitFence(fence);
    assert(bridge.Present(0x10100)==1);
    std::puts("GLES real clear, triangle, indexed triangle, pixel readback, Xbox texture upload, filtering, DXT fallback, depth, alpha blending, channel mask, 3D transforms, fence and pbuffer present passed");
}
