#include "xbe_gles_backend.h"
#include "xbe_d3d8_bridge.h"
#include "xbe_d3d8_create_probe.h"
#include "xbe_bc_decode.h"
#include "xbe_diagnostic_bitmap.h"
#include <fstream>
#include <cassert>
#include <cstdio>
using namespace hulkpocket;
int main(){
    {
        // Two top-down BGRA rows with padding: BMP must store bottom-up BGR.
        const uint8_t pixels[]={0,0,255,255,0,255,0,255,99,99,99,99,
                                255,0,0,255,255,255,255,255,88,88,88,88};
        const char* path="/tmp/hulk-replay-bitmap-test.bmp";
#ifndef __ANDROID__
        WriteDiagnosticBitmap(path,pixels,2,2,12);
        std::ifstream f(path,std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
        assert(bytes.size()==70&&bytes[0]=='B'&&bytes[1]=='M'&&bytes[10]==54);
        assert(bytes[54]==255&&bytes[55]==0&&bytes[56]==0&&bytes[60]==0&&bytes[61]==0);
        assert(bytes[62]==0&&bytes[63]==0&&bytes[64]==255&&bytes[65]==0&&bytes[66]==255&&bytes[67]==0);
        f.close();std::remove(path);
#endif
    }
    {
        XboxGuestAddressSpace guest;guest.Commit(0x20000,0x10000);
        XbeKernelPool pool(guest,0x09000000,0x10000);
        XbePhysicalMemory phys(guest);
        constexpr uint32_t dev=0x20000,slot=0x23000,pp=0x23100;
        guest.Write32(pp,64);guest.Write32(pp+4,64);guest.Write32(pp+8,6);
        guest.Write32(pp+12,1);guest.Write32(pp+0x20,1);guest.Write32(pp+0x24,0x2a);
        RunD3dCreateProbe(guest,pool,phys,dev,slot,pp);
        assert(pool.Outstanding()==0&&phys.AllocatedPages()==0&&guest.Read32(slot)==0);
        // Real two-buffer swap, stable header, distinct physical pixels.
        {
            XbeD3d8DeviceMemory buffers(guest,pool,phys);
            buffers.Initialize(dev,slot,pp);
            XbeGlesBackend gl;XbeD3d8Bridge bridge(guest,gl);
            bridge.Initialize(64,64);
            auto front=buffers.BackBuffer(-1),back=buffers.BackBuffer(0);
            auto a=guest.Read32(front+4),b=guest.Read32(back+4);
            bridge.BindInitialTargets(back,buffers.Depth());
            gpu::ScopedGuestMemory scope(guest);
            auto state=bridge.Snapshot();
            gl.Clear(state,0,nullptr,0xf3,0xffff0000,1,0);
            gl.ReadbackColor(state.rt);
            assert(guest.Read32(0x80000000u+b)==0xffff0000);
            assert(bridge.Present(back)==1&&buffers.Rotate()==1);
            assert(guest.Read32(front+4)==b&&guest.Read32(back+4)==a);
            bridge.BindInitialTargets(back,buffers.Depth());
            state=bridge.Snapshot();gl.BeginRotatedFrame(state.rt);
            bool refused=false;
            try{gl.ReadbackColor(state.rt);}catch(const std::logic_error&){refused=true;}
            assert(refused);
            refused=false;
            try{gl.Present(state.rt);}catch(const std::logic_error&){refused=true;}
            assert(refused);
            refused=false;
            try{gl.Clear(state,0,nullptr,0x10,0xff0000ff,1,0);}catch(const std::logic_error&){refused=true;}
            assert(refused);
            refused=false;
            try{gl.RestoreRotatedColor(state.rt);}catch(const std::logic_error&){refused=true;}
            assert(refused); // first rotation points to never-rendered backing
            gl.Clear(state,0,nullptr,0xf3,0xff0000ff,1,0);
            gl.ReadbackColor(state.rt);
            assert(guest.Read32(0x80000000u+a)==0xff0000ff);
            assert(guest.Read32(0x80000000u+b)==0xffff0000);
            assert(bridge.Present(back)==2);
            // Exercise multiple rotations and ensure the prior front
            // allocation survives the subsequent frame's clear.
            assert(buffers.Rotate()==2);
            for(unsigned frame=3;frame<=8;++frame){
                auto old_front=guest.Read32(front+4),old_back=guest.Read32(back+4);
                const auto previous_pixel=guest.Read32(0x80000000u+old_front);
                bridge.BindInitialTargets(back,buffers.Depth());
                state=bridge.Snapshot();gl.BeginRotatedFrame(state.rt);
                const uint32_t color=0xff000000u|(frame*0x010203u);
                gl.Clear(state,0,nullptr,0xf3,color,1,0);
                gl.ReadbackColor(state.rt);
                assert(guest.Read32(0x80000000u+old_back)==color);
                assert(guest.Read32(0x80000000u+old_front)==previous_pixel);
                assert(bridge.Present(back)==frame&&buffers.Rotate()==frame);
                assert(guest.Read32(front+4)==old_back&&guest.Read32(back+4)==old_front);
                assert(guest.Read32(dev+0x2478)==frame);
            }
            // Two already rendered buffers: swap with no draw/clear.
            bridge.BindInitialTargets(back,buffers.Depth());
            state=bridge.Snapshot();gl.BeginRotatedFrame(state.rt);
            auto prior=guest.Read32(0x80000000u+guest.Read32(back+4));
            gl.RestoreRotatedColor(state.rt);
            assert((gl.ReadPixel(0,0)==std::array<uint8_t,4>{uint8_t(prior>>16),uint8_t(prior>>8),uint8_t(prior),uint8_t(prior>>24)}));
            // A nonuniform top row checks BGRA and vertical orientation.
            int32_t top[]={0,0,64,1};
            gl.Clear(state,1,reinterpret_cast<const uint8_t*>(top),0xf0,0xff123456,1,0);
            gl.ReadbackColor(state.rt);
            assert(bridge.Present(back)==9&&buffers.Rotate()==9);
            bridge.BindInitialTargets(back,buffers.Depth());
            state=bridge.Snapshot();gl.BeginRotatedFrame(state.rt);
            gl.RestoreRotatedColor(state.rt);
            gl.ReadbackColor(state.rt);
            assert(bridge.Present(back)==10&&buffers.Rotate()==10);
            bridge.BindInitialTargets(back,buffers.Depth());
            state=bridge.Snapshot();gl.BeginRotatedFrame(state.rt);
            gl.RestoreRotatedColor(state.rt);
            assert((gl.ReadPixel(0,0)==std::array<uint8_t,4>{0x12,0x34,0x56,0xff}));
            assert((gl.ReadPixel(0,63)==std::array<uint8_t,4>{uint8_t(prior>>16),uint8_t(prior>>8),uint8_t(prior),uint8_t(prior>>24)}));
            gl.ReadbackColor(state.rt);
            assert(bridge.Present(back)==11&&buffers.Rotate()==11);
        }
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
        // Re-run from a genuinely populated guest viewport; the replay
        // must use it as-is and preserve the device fields.
        mem.Write32(dev+0xee8,64);mem.Write32(dev+0xeec,64);
        assert(bridge.SyncFixedDiffuseShader(dev)==0x42);
        call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.0f),0});
        bridge.ReplayFixedLinesDiagnostic(0,2);
        replay_pixel=false;
        for(unsigned y=30;y<=33;++y)
            if(backend.ReadPixel(32,y)==std::array<uint8_t,4>{255,0,255,255})replay_pixel=true;
        assert(replay_pixel&&bridge.Snapshot().viewport.w==64&&mem.Read32(dev+0xee8)==64);
        auto first_profile=bridge.Snapshot();
        // Second line uses a different buffer and leaves the first line in
        // the framebuffer. No intervening clear, just as observed on RP6.
        mem.Write32(0x10220,0x01080001);mem.Write32(0x10224,0x106000);
        const XYZ second[]={{-.75f,.5f,.5f,0xff00ffff},{.75f,.5f,.5f,0xff00ffff}};
        mem.Write(0x80106000,second,sizeof(second));
        call("D3DDevice_SetStreamSource",{0,0x10220,sizeof(XYZ)});
        assert(XbeD3d8Bridge::SameDiagnosticLineProfile(first_profile,bridge.Snapshot()));
        bridge.ReplayFixedLinesDiagnostic(0,2);
        bool second_pixel=false,first_pixel=false;
        for(unsigned y=14;y<=17;++y)
            if(backend.ReadPixel(32,y)==std::array<uint8_t,4>{0,255,255,255})second_pixel=true;
        for(unsigned y=30;y<=33;++y)
            if(backend.ReadPixel(32,y)==std::array<uint8_t,4>{255,0,255,255})first_pixel=true;
        assert(first_pixel&&second_pixel);
        auto incompatible=bridge.Snapshot();incompatible.simple[gpu::kMBlendEnable>>2]^=1;
        assert(!XbeD3d8Bridge::SameDiagnosticLineProfile(first_profile,incompatible));
        mem.Write32(dev+0xee8,32);
        assert(bridge.SyncFixedDiffuseShader(dev)==0x42);
        bool partial_rejected=false;
        try{bridge.ReplayFixedLinesDiagnostic(0,2);}catch(const std::logic_error&){partial_rejected=true;}
        assert(partial_rejected&&bridge.Snapshot().viewport.w==32);
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
    // Recreate the RP6 original XYZ/diffuse/UV declaration (24-byte
    // stride), one swizzled texture, and the observed combiner order.
    {
        constexpr uint32_t dev=0x16000,shader=0x19000;
        mem.Write32(dev+0x794,shader);mem.Write32(shader+4,0x400);
        for(unsigned i=0;i<16;++i){
            const uint32_t a=shader+0x14+16*i;
            mem.Write32(a,0);mem.Write32(a+4,0);mem.Write32(a+8,2);mem.Write32(a+12,0);
        }
        mem.Write32(shader+0x14+8,0x32);
        mem.Write32(shader+0x14+3*16+4,12);mem.Write32(shader+0x14+3*16+8,0x40);
        mem.Write32(shader+0x14+9*16+4,16);mem.Write32(shader+0x14+9*16+8,0x22);
        for(unsigned tr:{0u,1u,6u})mem.Write(dev+0xc60+64*tr,identity,sizeof(identity));
        const uint32_t viewport[]={0,0,64,64,0,0x3f800000,0x3f080000,0x3f080000};
        mem.Write(dev+0xee0,viewport,sizeof(viewport));
        mem.Write32(dev+0xf88,0x10400);
        mem.Write32(0x1040c,0x01110621);mem.Write(0x80106000,tex,sizeof(tex));
        const uint32_t push[]={0x1b00|(2u<<18),0x106000,0x01110621,0x1b0c|(1u<<18),0};
        mem.Write(0x11800,push,sizeof(push));
        bridge.ApplyPush(0x11800,0x11800+sizeof(push));
        auto profile=bridge.Snapshot();profile.rs.fill(0);profile.tss={};
        profile.simple.fill(0);profile.simple[gpu::kMColorWriteEnable>>2]=0x01010101;
        // AdoptLiveSetters also mirrors raw state; retain the verified pair.
        profile.simple[0x1b00>>2]=0x106000;profile.simple[0x1b04>>2]=0x01110621;
        auto& st=profile.tss[0];
        st[gpu::kTssAddressU]=st[gpu::kTssAddressV]=st[gpu::kTssAddressW]=3;
        st[gpu::kTssMinFilter]=st[gpu::kTssMagFilter]=2;
        st[gpu::kTssMaxAnisotropy]=1;st[gpu::kTssResultArg]=1;
        st[gpu::kTssColorOp]=st[gpu::kTssAlphaOp]=4;
        st[gpu::kTssColorArg2]=st[gpu::kTssAlphaArg2]=2;
        for(unsigned i=1;i<4;++i)profile.tss[i][gpu::kTssColorOp]=profile.tss[i][gpu::kTssAlphaOp]=1;
        bridge.AdoptLiveSetters(profile);
        struct QuadVertex {float x,y,z;uint32_t color;float u,v;};
        const QuadVertex quad[]={{-1,1,.5f,0xffffffff,0,0},{1,1,.5f,0xffffffff,1,0},
                                 {-1,-1,.5f,0xffffffff,0,1},{1,-1,.5f,0xffffffff,1,1}};
        mem.Write(0x80105000,quad,sizeof(quad));
        call("D3DDevice_SetStreamSource",{0,0x10200,sizeof(QuadVertex)});
        assert(bridge.SyncFixedDiffuseShader(dev)==0x142);
        assert(bridge.VerifyGuestGeometry(6,0,4,0)==4);
        call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.0f),0});
        bridge.ReplayTexturedQuadDiagnostic(0,4);
        assert((backend.ReadPixel(8,8)==std::array<uint8_t,4>{255,0,0,255}));
        assert((backend.ReadPixel(56,56)==std::array<uint8_t,4>{255,255,255,255}));
        assert(bridge.Snapshot().tss[0][gpu::kTssColorArg1]==0);
        const auto first_profile=bridge.Snapshot();
        auto next_profile=first_profile;
        next_profile.textures[0]=0x10420;
        next_profile.simple[0x1b00>>2]=0x107000;
        next_profile.transforms[256][12]=.25f;
        assert(XbeD3d8Bridge::SameDiagnosticTextureProfile(first_profile,next_profile));
        next_profile.tss[0][gpu::kTssMagFilter]=1;
        assert(!XbeD3d8Bridge::SameDiagnosticTextureProfile(first_profile,next_profile));
        next_profile=first_profile;
        next_profile.simple[gpu::kMBlendEnable>>2]^=1;
        assert(!XbeD3d8Bridge::SameDiagnosticTextureProfile(first_profile,next_profile));
        next_profile=first_profile;next_profile.textures[1]=0x10420;
        assert(!XbeD3d8Bridge::SameDiagnosticTextureProfile(first_profile,next_profile));
        // Actual second draw: replace the texture at a different guest
        // physical address and verify that the new texels are sampled.
        mem.Write32(0x10404,0x107000);
        const uint32_t green[]={0xff00ff00,0xff00ff00,0xff00ff00,0xff00ff00};
        mem.Write(0x80107000,green,sizeof(green));
        const uint32_t second_push[]={0x1b00|(2u<<18),0x107000,0x01110621};
        mem.Write(0x11800,second_push,sizeof(second_push));
        bridge.ApplyPush(0x11800,0x11800+sizeof(second_push));
        assert(bridge.SyncFixedDiffuseShader(dev)==0x142);
        assert(XbeD3d8Bridge::SameDiagnosticTextureProfile(first_profile,bridge.Snapshot()));
        bridge.ReplayTexturedQuadDiagnostic(0,4);
        assert((backend.ReadPixel(8,8)==std::array<uint8_t,4>{0,255,0,255}));
        bool guarded=false;
        try{call("D3DDevice_DrawVertices",{6,0,4});}catch(const std::logic_error&){guarded=true;}
        assert(guarded);
        mem.Write32(0x10404,0x106000);
        bool mismatch=false;
        try{bridge.SyncFixedDiffuseShader(dev);}catch(const std::logic_error&){mismatch=true;}
        assert(mismatch);
        // Linear X8R8G8B8 with padded rows, texel coordinates, point filter,
        // and two triangles: the next original RP6 movie-style draw.
        mem.Write32(0x10404,0x107000);
        mem.Write32(0x1040c,0x00011e21);mem.Write32(0x10410,0x00001001);
        const uint32_t top[]={0x00123456,0x00ff0000},bottom[]={0x0000ff00,0x000000ff};
        mem.Write(0x80107000,top,sizeof(top));mem.Write(0x80107040,bottom,sizeof(bottom));
        const uint32_t linear_push[]={0x1b00|(2u<<18),0x107000,0x00011e21,
            0x1b10|(1u<<18),0x00400000,0x1b1c|(1u<<18),0x00020002,0x1b0c|(1u<<18),0};
        mem.Write(0x11800,linear_push,sizeof(linear_push));bridge.ApplyPush(0x11800,0x11800+sizeof(linear_push));
        auto linear=bridge.Snapshot();
        linear.tss[0][gpu::kTssMinFilter]=linear.tss[0][gpu::kTssMagFilter]=1;
        bridge.AdoptLiveSetters(linear);
        const QuadVertex triangles[]={{-1,1,.5f,0xffffffff,0,0},{1,1,.5f,0xffffffff,2,0},
            {-1,-1,.5f,0xffffffff,0,2},{-1,-1,.5f,0xffffffff,0,2},
            {1,1,.5f,0xffffffff,2,0},{1,-1,.5f,0xffffffff,2,2}};
        mem.Write(0x80105000,triangles,sizeof(triangles));
        assert(bridge.SyncFixedDiffuseShader(dev)==0x142);
        assert(bridge.VerifyGuestGeometry(5,0,6,0)==6);
        bridge.ReplayTexturedDiffuseDiagnostic(5,0,6);
        assert((backend.ReadPixel(8,8)==std::array<uint8_t,4>{0x12,0x34,0x56,255}));
        assert((backend.ReadPixel(56,56)==std::array<uint8_t,4>{0,0,255,255}));
        assert((backend.ReadPixel(8,56)==std::array<uint8_t,4>{0,255,0,255}));
        mem.Write32(0x10410,0x01001001); // pitch mismatch, reject
        mismatch=false;
        try{bridge.SyncFixedDiffuseShader(dev);}catch(const std::logic_error&){mismatch=true;}
        assert(mismatch);
        // DXT1 mip chain (4x4 red, 2x2 green, 1x1 blue), repeat,
        // minification and an indexed strip, matching the next RP6 draw family.
        mem.Write32(0x1040c,0x02230c21);mem.Write32(0x10410,0);
        const uint8_t chain[]={0,0xf8,0,0,0,0,0,0, 0xe0,7,0,0,0,0,0,0, 0x1f,0,0,0,0,0,0,0};
        mem.Write(0x80107000,chain,sizeof(chain));
        const uint32_t bc_push[]={0x1b00|(2u<<18),0x107000,0x02230c21,0x1b0c|(1u<<18),0};
        mem.Write(0x11800,bc_push,sizeof(bc_push));bridge.ApplyPush(0x11800,0x11800+sizeof(bc_push));
        auto mip=bridge.Snapshot();
        mip.tss[0][gpu::kTssAddressU]=mip.tss[0][gpu::kTssAddressV]=mip.tss[0][gpu::kTssAddressW]=1;
        mip.tss[0][gpu::kTssMinFilter]=mip.tss[0][gpu::kTssMagFilter]=2;
        mip.tss[0][gpu::kTssMipFilter]=2;
        bridge.AdoptLiveSetters(mip);
        const QuadVertex minified[]={{-1,1,.5f,0xffffffff,0,0},{1,1,.5f,0xffffffff,128,0},
            {-1,-1,.5f,0xffffffff,0,128},{1,-1,.5f,0xffffffff,128,128}};
        mem.Write(0x80105000,minified,sizeof(minified));
        const uint16_t idx[]={0,1,2,3};mem.Write(0x11880,idx,sizeof(idx));
        assert(bridge.SyncFixedDiffuseShader(dev)==0x142);
        assert(bridge.VerifyGuestGeometry(6,0,4,0x11880)==4);
        bridge.ReplayTexturedDiffuseDiagnostic(6,0,4,0x11880);
        assert((backend.ReadPixel(32,32)==std::array<uint8_t,4>{0,0,255,255}));
        // Reproduce the next APK declaration; packed normals must not be
        // interpreted as vertex colors. Verify geometry but do not draw it
        // through an unlit shader.
        auto shader=mem.Read32(dev+0x794);
        mem.Write32(shader+4,0);
        mem.Write32(shader+0x14+3*16+4,0);mem.Write32(shader+0x14+3*16+8,2);
        mem.Write32(shader+0x14+2*16+4,12);mem.Write32(shader+0x14+2*16+8,0x16);
        const uint32_t attributes[]={0x194c|(4u<<18),0xffffffff,0,0,0};
        mem.Write(0x11800,attributes,sizeof(attributes));bridge.ApplyPush(0x11800,0x11800+sizeof(attributes));
        auto lit=bridge.Snapshot();lit.rs[gpu::kRsLighting]=1;lit.rs[gpu::kRsSpecularEnable]=1;
        bridge.AdoptLiveSetters(lit);
        assert(bridge.SyncFixedDiffuseShader(dev)==(shader|1u));
        assert(bridge.VerifyGuestGeometry(6,0,4,0x11880)==4);
        bool lit_blocked=false;
        try{bridge.ReplayTexturedDiffuseDiagnostic(6,0,4,0x11880);}catch(const std::logic_error&){lit_blocked=true;}
        assert(lit_blocked);
    }
    std::puts("GLES real clear, triangle, indexed triangle, pixel readback, Xbox texture upload, filtering, DXT fallback, depth, alpha blending, channel mask, 3D transforms, fence and pbuffer present passed");
}
