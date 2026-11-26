#include "hulk_live_fibers.h"
#include "xbox_guest_address_space.h"
#include <cassert>
#include <vector>
using namespace hulkpocket;
namespace {
constexpr uint32_t a=0x10000,b=0x10100,tls=0x10200,fs=0x10300;
constexpr uint32_t stack_a=0x30000,stack_b=0x40000,stack_base=0x20000;
struct Fixture {
    XboxGuestAddressSpace mem;
    X86Context ctx{};
    std::vector<int> steps;
    Fixture(){
        mem.Commit(a,0x2000);mem.Commit(stack_base,0x1000);
        mem.Commit(stack_a,0x2000);mem.Commit(stack_b,0x2000);
        mem.Commit(kHulkUsaFiberLayout.fiber_tls_index_va,4);
        ctx.host=this;ctx.fs_base=fs;ctx.esp=stack_a+0x1000;
        ctx.ebx=0xa001;ctx.ebp=0xa004;
        mem.Write32(fs,0x7777);mem.Write32(fs+4,stack_base);
        mem.Write32(kHulkUsaFiberLayout.fiber_tls_index_va,3);
        mem.Write32(stack_base+12,tls);mem.Write32(tls+0x10,a);
        const uint32_t frame[]={0x5555,0xb001,0xb002,0xb003,0xb004,kHulkUsaFiberLayout.bootstrap_va,0xdead};
        for(unsigned i=0;i<7;++i)mem.Write32(stack_b+0x1000+4*i,frame[i]);
        mem.Write32(b+0xc,stack_b+0x1000);
    }
    static void Memory(X86Context& c,uint8_t* base,uint32_t op){
        auto& f=*static_cast<Fixture*>(c.host);
        assert(base==f.mem.Base());
        if(op==kFiberRead32)c.eax=f.mem.Read32(c.ecx);
        else if(op==kFiberWrite32)f.mem.Write32(c.ecx,c.eax);
        else std::abort();
    }
    void Switch(uint32_t next){
        mem.Write32(ctx.esp,0x2655e9);mem.Write32(ctx.esp+4,next);
        HulkLiveFibers::SwitchGuest(ctx,mem.Base());
    }
    static void Guest(X86Context& c,uint8_t* base,uint32_t entry){
        auto& f=*static_cast<Fixture*>(c.host);assert(&c==&f.ctx&&base==f.mem.Base());
        if(entry==0x20a016){
            f.steps.push_back(1);
            f.Switch(b);
            assert(c.esp==stack_a+0x1008&&c.ebx==0xa001&&c.ebp==0xa004&&f.mem.Read32(tls+0x10)==a);
            f.steps.push_back(3);
            f.Switch(b);
            assert(c.esp==stack_a+0x1010&&f.mem.Read32(tls+0x10)==a);
            f.steps.push_back(5);
            f.mem.Write32(c.esp+4,b);
            HulkLiveFibers::DeleteGuest(c,base);
        }else{
            assert(entry==kHulkUsaFiberLayout.bootstrap_va&&c.esp==stack_b+0x101c&&c.ebx==0xb001);
            f.steps.push_back(2);
            f.Switch(a);
            assert(c.esp==stack_b+0x1024&&c.ebp==0xb004&&f.mem.Read32(tls+0x10)==b);
            f.steps.push_back(4);
            f.Switch(a);
            std::abort(); // deleted while suspended
        }
    }
};
}
int main(){
    Fixture f;
    ScopedGraphicsCallback checked(Fixture::Memory);
    HulkLiveFibers scope(f.ctx,f.mem.Base(),Fixture::Guest,kHulkUsaFiberLayout);
    scope.Run(0x20a016);
    assert(f.steps==std::vector<int>({1,2,3,4,5})&&!HulkLiveFibers::Active());
}
