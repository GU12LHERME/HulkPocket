#include "xbe_d3d8_bridge.h"
#include "xbe_d3d8_device_memory.h"
#include "xbe_nv2a_push.h"
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
        bool invalid_packet=false;
        try{DecodeNv2aMethods(guest,0x24000,0x24000+sizeof(packets)-4);}catch(const std::invalid_argument&){invalid_packet=true;}
        assert(invalid_packet);
        guest.Write32(0x24000,0x20000000);
        invalid_packet=false;
        try{DecodeNv2aMethods(guest,0x24000,0x24004);}catch(const std::invalid_argument&){invalid_packet=true;}
        assert(invalid_packet);
        XbeKernelPool pool(guest,0x09000000,0x10000);
        XbePhysicalMemory physical(guest);
        constexpr uint32_t dev=0x20000,slot=0x23000,pp=0x23100;
        guest.Write32(pp,640);guest.Write32(pp+4,480);guest.Write32(pp+8,6);
        guest.Write32(pp+12,1);guest.Write32(pp+0x20,1);guest.Write32(pp+0x24,0x2a);
        {
            XbeD3d8DeviceMemory device(guest,pool,physical);
            device.Initialize(dev,slot,pp);
            assert(device.BufferCount()==2&&guest.Read32(slot)==dev);
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
    call("D3DDevice_Clear",{0,0,0xf0,0xff123456,std::bit_cast<uint32_t>(1.0f),0});
    mem.Write32(0x10200,0x40000);mem.Write32(0x10204,0x100100);
    float v[]={0,0,0,1,0,0,0,1,0};mem.Write(0x80100100,v,sizeof(v));
    call("D3DDevice_SetStreamSource",{0,0x10200,12});
    call("D3DDevice_SetVertexShader",{2});
    call("D3DDevice_DrawVertices",{5,0,3});
    uint16_t indices[]={0,1,2};mem.Write(0x10300,indices,sizeof(indices));
    call("D3DDevice_DrawIndexedVertices",{5,3,0x10300});
    assert(bridge.Present(0x10100)==1 && backend.draws==2&&backend.clears==1&&backend.presents==1);
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
