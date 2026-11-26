#include "xbe_d3d8_bridge.h"
#include "xbe_d3d8_device_memory.h"
#include "xbe_nv2a_push.h"
#include "xbe_d3d8_default_trace.h"
#include <cassert>
using namespace hulkpocket;
class RecordingBackend final:public gpu::Backend {
public:
    unsigned clears=0,draws=0,presents=0;
    bool Init(uint32_t w,uint32_t h)override{return w==640&&h==480;}
    void Clear(const gpu::State& s,uint32_t n,const uint8_t* p,uint32_t flags,uint32_t color,float z,uint32_t stencil)override{
        assert(s.rt.header==0x10100 && n==0 && p==nullptr && flags==0xf0 && color==0xff123456 && z==1 && stencil==0);++clears;
    }
    void Draw(const gpu::State& s,uint32_t prim,uint32_t start,uint32_t count,const uint8_t* indices)override{
        gpu::Assembled draw;
        assert(gpu::Assemble(s,prim,start,count,indices,draw));
        assert(draw.vertex_count==3);++draws;
    }
    void Present(const gpu::Surface& s)override{assert(s.header==0x10100);++presents;}
    void Copy(const gpu::Surface&,const gpu::Surface&,uint32_t,uint32_t,uint32_t,float,float)override{throw std::logic_error("not tested");}
    void Sync(uint32_t,uint32_t)override{throw std::logic_error("not tested");}
    void SyncLate(uint32_t)override{throw std::logic_error("not tested");}
    void Flush()override{}
    void SetGamma(const uint8_t*)override{throw std::logic_error("not tested");}
};
int main(){
    {
        XboxGuestAddressSpace guest;guest.Commit(0x20000,0x10000);
        const uint32_t packets[]={0x400|(2u<<18),7,8,0x40000420|(2u<<18),9,10};
        guest.Write(0x24000,packets,sizeof(packets));
        auto methods=DecodeNv2aMethods(guest,0x24000,0x24000+sizeof(packets));
        assert(methods.size()==4&&methods[0].method==0x400&&methods[1].method==0x404);
        assert(methods[2].method==0x420&&methods[3].method==0x420&&methods[3].value==10);
        gpu::State pushed{};Nv2aConstantCursor cursor{};
        ApplyNv2aState(pushed,cursor,{{gpu::kMBlendEnable,1},{0x1ea4,8},{0xb80,0x3f800000},{0xb84,0x40000000}});
        assert(pushed.simple[gpu::kMBlendEnable>>2]==1&&pushed.vsc[8][0]==1&&pushed.vsc[8][1]==2);
        ApplyNv2aState(pushed,cursor,{{0xb88,0x40400000},{0xb8c,0x40800000}});
        assert(cursor.reg==9&&cursor.component==0&&pushed.vsc[8][3]==4);
        // Exact RP6 post-CreateDevice packet batch from ef17d47.
        std::vector<Nv2aMethod> disable;
        for(uint32_t stage=0;stage<4;++stage){
            disable.push_back({0x1b0c+stage*0x40,0});
            for(uint32_t coord=0;coord<3;++coord)disable.push_back({0x03c0+stage*0x10+coord*4,0});
        }
        assert(disable.size()==16);
        for(auto m:disable)pushed.simple[m.method>>2]=0x1234;
        ApplyNv2aState(pushed,cursor,disable);
        for(auto m:disable)assert(pushed.simple[m.method>>2]==0);
        bool enabled_rejected=false;
        try{ApplyNv2aState(pushed,cursor,{{gpu::kMBlendEnable,0},{0x1b0c,0x40000000}});}
        catch(const std::logic_error&){enabled_rejected=true;}
        assert(enabled_rejected&&pushed.simple[gpu::kMBlendEnable>>2]==1);
        enabled_rejected=false;
        try{ApplyNv2aState(pushed,cursor,{{0x03c0,0x2400}});}
        catch(const std::logic_error&){enabled_rejected=true;}
        assert(enabled_rejected&&pushed.simple[0x03c0>>2]==0);
        // Complete 56-command RP6 batch from b0e67b8, in original order.
        std::vector<Nv2aMethod> config;
        for(uint32_t stage=1;stage<4;++stage)
            for(uint32_t m=0x28;m<=0x3c;m+=4)config.push_back({0x1b00+stage*0x40+m,0});
        config.insert(config.end(),{{0x1e78,0x00210000},{0x1be8,0x3f800000},
            {0x1bec,0},{0x1bf4,0},{0x1bf0,0x3f800000}});
        for(unsigned repeat=0;repeat<3;++repeat)
            config.insert(config.end(),{{0x0200,0},{0x0204,0},{0x02b4,0},{0x02c0,0},{0x02e0,0},
                {0x0a20,0x3f080000},{0x0a24,0x3f080000},{0x0a28,0},{0x0a2c,0},{0x0394,0},{0x0398,0x4b7fffff}});
        assert(config.size()==56);
        pushed.simple[0x0200>>2]=0x1234;
        auto before_config=pushed.simple;
        auto bad_config=config;bad_config.push_back({0x17fc,5});
        bool config_rejected=false;
        try{ApplyNv2aState(pushed,cursor,bad_config);}catch(const std::logic_error&){config_rejected=true;}
        assert(config_rejected&&pushed.simple==before_config&&!cursor.pending_raw_configuration);
        ApplyNv2aState(pushed,cursor,config);
        assert(cursor.pending_raw_configuration&&pushed.simple[0x0200>>2]==0);
        assert(pushed.simple[0x1e78>>2]==0x00210000&&pushed.simple[0x1be8>>2]==0x3f800000);
        assert(pushed.simple[0x1bf0>>2]==0x3f800000&&pushed.simple[0x0398>>2]==0x4b7fffff);
        ApplyNv2aState(pushed,cursor,{{0x1ea4,8}});
        assert(cursor.pending_raw_configuration);
        bool state_rejected=false;
        try{ApplyNv2aState(pushed,cursor,{{gpu::kMBlendEnable,0},{0x17fc,5}});}
        catch(const std::logic_error&){state_rejected=true;}
        assert(state_rejected&&pushed.simple[gpu::kMBlendEnable>>2]==1);
        bool invalid_packet=false;
        try{DecodeNv2aMethods(guest,0x24000,0x24000+sizeof(packets)-4);}catch(const std::invalid_argument&){invalid_packet=true;}
        assert(invalid_packet);
        guest.Write32(0x24000,0x20000000);
        invalid_packet=false;
        try{DecodeNv2aMethods(guest,0x24000,0x24004);}catch(const std::invalid_argument&){invalid_packet=true;}
        assert(invalid_packet);
        XbeD3dDefaultTrace trace(guest);
        guest.Write32(0x25000,gpu::kMBlendEnable);
        trace.Render(0x25000,0x25400,0,1);
        trace.Render(0,0x25400,gpu::kRsZEnable,1);
        trace.Texture(0x25800,2,gpu::kTssColorOp,4);
        assert(trace.State().simple[gpu::kMBlendEnable>>2]==1);
        assert(guest.Read32(0x25400+4*gpu::kRsZEnable)==1);
        assert(guest.Read32(0x25800+4*(64+gpu::kTssColorOp))==4);
        XbeKernelPool pool(guest,0x09000000,0x10000);
        XbePhysicalMemory physical(guest);
        constexpr uint32_t dev=0x20000,slot=0x23000,pp=0x23100;
        guest.Write32(pp,640);guest.Write32(pp+4,480);guest.Write32(pp+8,6);
        guest.Write32(pp+12,1);guest.Write32(pp+0x20,1);guest.Write32(pp+0x24,0x2a);
        {
            XbeD3d8DeviceMemory device(guest,pool,physical);
            device.Initialize(dev,slot,pp);
            assert(device.BufferCount()==2&&guest.Read32(slot)==dev);
            assert(device.Scratch()&&device.ScratchEnd()==device.Scratch());
            device.BindInitialFvf(0x25c00);
            assert(guest.Read32(dev+0x794)==0x25c00);
            bool bad_fvf=false;
            try{device.BindInitialFvf(0);}catch(const std::invalid_argument&){bad_fvf=true;}
            assert(bad_fvf&&guest.Read32(dev+0x794)==0x25c00);
            assert(guest.Read32(dev+4)==device.Scratch()+0xf000);
            auto front=device.BackBuffer(-1),back=device.BackBuffer(0),depth=device.Depth();
            assert(front&&back&&depth&&front!=back);
            assert(guest.Read32(dev+0x1a04)==back&&guest.Read32(dev+0x1a08)==depth);
            assert(guest.Read32(back+12)==0x00011221&&guest.Read32(depth+12)==0x00012e21);
            auto a=guest.Read32(front+4),b=guest.Read32(back+4);
            assert(a!=b&&physical.Contains(0x80000000u+b,640*480*4));
            assert(device.Rotate()==1&&guest.Read32(front+4)==b&&guest.Read32(back+4)==a);
            assert(guest.Read32(dev+0x2478)==1);
            assert(device.Rotate()==2&&guest.Read32(front+4)==a);
        }
        assert(pool.Outstanding()==0&&physical.AllocatedPages()==0&&guest.Read32(slot)==0);
    }
    XboxGuestAddressSpace mem;mem.Commit(0x10000,0x10000);mem.Commit(0x80100000,0x10000);
    RecordingBackend backend;XbeD3d8Bridge bridge(mem,backend);bridge.Initialize(640,480);
    X86Context c{};
    auto call=[&](std::string_view name,std::initializer_list<uint32_t> args){
        c.esp=0x1f000;mem.Write32(c.esp,0x20000);
        unsigned i=0;for(auto v:args)mem.Write32(c.esp+4+4*i++,v);
        bridge.Call(name,c,D3dAbi::Stdcall(unsigned(args.size())));
        assert(c.esp==0x1f004+4*args.size()&&c.eax==0);
    };
    mem.Write32(0x10100,0x50000);mem.Write32(0x10104,0x100000);mem.Write32(0x1010c,0x00011221);
    call("D3DDevice_SetRenderTarget",{0x10100,0});
    // Verify default-state handoff preserves target/viewport and raw NV2A
    // values, and rejects an unknown packet without partial mutation.
    gpu::State captured{};
    captured.rs[gpu::kRsZEnable]=1;
    captured.simple[gpu::kMColorWriteEnable>>2]=0x01010101;
    captured.tss[0][gpu::kTssColorOp]=4;
    const uint32_t defaults[]={0x0a20|(2u<<18),0x3f080000,0x3f080000,
        0x0394|(2u<<18),0,0x4b7fffff,0x1b0c|(1u<<18),0};
    mem.Write(0x10400,defaults,sizeof(defaults));
    bridge.AdoptOriginalDefaults(captured,0x10400,0x10400+sizeof(defaults));
    auto adopted=bridge.Snapshot();
    assert(adopted.rt.header==0x10100&&adopted.viewport.w==640);
    assert(adopted.rs==captured.rs&&adopted.tss==captured.tss);
    assert(adopted.simple[0x0a20>>2]==0x3f080000&&adopted.simple[0x0398>>2]==0x4b7fffff);
    mem.Write32(0x10400,0x17fc|(1u<<18));
    bool bad_default=false;
    try{bridge.AdoptOriginalDefaults(gpu::State{},0x10400,0x10408);}
    catch(const std::logic_error&){bad_default=true;}
    assert(bad_default&&bridge.Snapshot().simple==adopted.simple&&bridge.Snapshot().rs==adopted.rs);
    XbeD3dDefaultTrace live_trace(mem,bridge.Snapshot());
    mem.Write32(0x11000,gpu::kMBlendEnable);
    live_trace.Render(0x11000,0x11400,0,1);
    live_trace.Render(0,0x11400,gpu::kRsZEnable,0);
    live_trace.Texture(0x11800,2,gpu::kTssColorOp,4);
    bridge.AdoptLiveSetters(live_trace.State());
    auto updated=bridge.Snapshot();
    assert(updated.rt.header==adopted.rt.header&&updated.viewport.w==adopted.viewport.w);
    assert(updated.simple[0x0a20>>2]==adopted.simple[0x0a20>>2]);
    assert(updated.simple[gpu::kMBlendEnable>>2]==1&&updated.rs[gpu::kRsZEnable]==0);
    assert(updated.tss[2][gpu::kTssColorOp]==4&&updated.tss[0][gpu::kTssColorOp]==4);
    assert(mem.Read32(0x11400+4*gpu::kRsZEnable)==0);
    assert(mem.Read32(0x11800+4*(64+gpu::kTssColorOp))==4);
    call("D3DDevice_Clear",{0,0,0xf0,0xff123456,std::bit_cast<uint32_t>(1.0f),0});
    mem.Write32(0x10200,0x40000);mem.Write32(0x10204,0x100100);
    float v[]={0,0,0,1,0,0,0,1,0};mem.Write(0x80100100,v,sizeof(v));
    call("D3DDevice_SetStreamSource",{0,0x10200,12});
    call("D3DDevice_SetVertexShader",{2});
    call("D3DDevice_DrawVertices",{5,0,3});
    uint16_t indices[]={0,1,2};mem.Write(0x10300,indices,sizeof(indices));
    call("D3DDevice_DrawIndexedVertices",{5,3,0x10300});
    assert(bridge.Present(0x10100)==1 && backend.draws==2&&backend.clears==1&&backend.presents==1);
    mem.Write32(0x10400,0x0200|(1u<<18));mem.Write32(0x10404,0);
    bridge.ApplyPush(0x10400,0x10408);
    bool raw_draw_blocked=false;
    try{call("D3DDevice_DrawVertices",{5,0,3});}
    catch(const std::logic_error&){raw_draw_blocked=true;}
    assert(raw_draw_blocked&&backend.draws==2&&c.esp==0x1f000);
    c.esp=0x1f000;c.ecx=0x100;c.edx=0x1234;
    bridge.Call("D3DDevice_SetRenderState_Simple",c,D3dAbi::Mapped({D3dArg::Ecx,D3dArg::Edx}));
    assert(c.esp==0x1f004 && bridge.Snapshot().simple[0x40]==0x1234);
    // LTCG argument mapping: register followed by stack, no guessed cleanup.
    c.esp=0x1f000;c.esi=0;mem.Write32(c.esp+4,0x10200);mem.Write32(c.esp+8,12);
    bridge.Call("D3DDevice_SetStreamSource",c,D3dAbi::Mapped({D3dArg::Esi,D3dArg::Stack,D3dArg::Stack}));
    assert(c.esp==0x1f00c);
    bool rejected=false;c.esp=0x1f000;mem.Write32(c.esp+4,0x30000);
    try{bridge.Call("D3DDevice_SetViewport",c,D3dAbi::Stdcall(1));}catch(const std::out_of_range&){rejected=true;}
    assert(rejected&&c.esp==0x1f000);
    rejected=false;
    try{bridge.Call("D3DDevice_Swap",c,D3dAbi::Stdcall(1));}catch(const std::logic_error&){rejected=true;}
    assert(rejected&&c.esp==0x1f000);
}
