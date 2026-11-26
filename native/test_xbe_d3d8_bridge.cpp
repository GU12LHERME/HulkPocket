#include "xbe_d3d8_bridge.h"
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
