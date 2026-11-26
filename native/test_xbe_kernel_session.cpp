#include "xbe_kernel_session.h"
#include <cassert>
#include <atomic>
using namespace hulkpocket;
namespace {
XboxGuestAddressSpace* memory;
std::atomic<unsigned> ran{0};
void call(X86Context& c, uint8_t* base,XbeKernelSession::Callback cb,
          uint32_t ordinal,std::initializer_list<uint32_t> args) {
    const auto old=c.esp;
    c.esp-=static_cast<uint32_t>(4+4*args.size());
    memory->Write32(c.esp,0);
    unsigned i=0;
    for(auto a:args) memory->Write32(c.esp+4+4*i++,a);
    cb(c,base,ordinal);
    assert(c.esp==old);
}
int entry(X86Context* c,uint8_t* base,uint32_t addr,XbeKernelSession::Callback cb) {
    if(addr==0x2000) {
        assert(memory->Read32(c->esp+4)==0x3000);
        assert(memory->Read32(c->esp+8)==0x1234);
        const auto thread=memory->Read32(c->fs_base+0x28);
        const auto tls=memory->Read32(thread+0x28);
        assert(memory->Read32(tls)==0xdeadbeef);
        assert(memory->Read32(tls+4)==0);
        call(*c,base,cb,104,{});
        assert(c->eax==thread);
        ++ran;
        call(*c,base,cb,258,{42});
        assert(false);
    }
    call(*c,base,cb,255,{0x10100,0,0x10000,8,0x10104,0x3000,0x1234,1,0,0x2000});
    assert(c->eax==0 && ran==0);
    const auto h=memory->Read32(0x10100);
    assert(h!=memory->Read32(0x10104));
    call(*c,base,cb,231,{h,0x10108});
    assert(memory->Read32(0x10108)==1);
    call(*c,base,cb,224,{h,0x10108});
    assert(memory->Read32(0x10108)==2 && ran==0);
    call(*c,base,cb,224,{h,0x10108});
    assert(memory->Read32(0x10108)==1);
    call(*c,base,cb,224,{0xdead,0});
    assert(c->eax==0xc0000008u);
    return 0;
}
}
int main() {
    XboxGuestAddressSpace mem;
    memory=&mem;
    mem.Commit(0x10000,0x10000);
    mem.Write32(0x10200,0xdeadbeef);
    XbeTlsTemplate tls{0x10200,0x10204,0,0,4,0};
    X86Context cpu{};
    cpu.esp=0x1fff0; cpu.thread_handle=0x10;
    XbeKernelSession session(mem,0x09000000,0x01000000,tls);
    assert(session.Run(entry,cpu,0x1000)==0);
    assert(ran==1);
    assert(mem.Read32(0x0A000404)==1);
}
