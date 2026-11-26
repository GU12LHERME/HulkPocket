#include "xbe_entry_session.h"
#include "xbe_kernel_session.h"
#include <cassert>
using namespace hulkpocket;
int main() {
    XboxGuestAddressSpace m;
    m.Commit(0x10000,0x10000);
    LoadedXbeImage i{};
    i.base=0x10000;i.image_size=0x10000;i.entry=0x15000;
    i.stack_commit=0x10000;i.heap_reserve=0x100000;i.heap_commit=0x1000;
    i.kernel_thunk=0x13000;
    m.Write32(0x13000,0x80000011);
    m.Write32(0x13004,0x80000010);
    m.Write32(0x13008,0);
    HulkKernelDataRegistry registry;
    bool rejected=false;
    try { PrepareEntrySession(i,m,registry,0x1000000,0x1200000,0x14000,1,0); }
    catch(const std::runtime_error&) { rejected=true; }
    assert(rejected && m.Read32(0x13000)==0x80000011);
    registry.Register(16,0x16000,32,m);
    auto s=PrepareEntrySession(i,m,registry,0x1000000,0x1200000,0x14000,1,0,0x17000);
    assert(m.Read32(0x1401c)==0x14000);
    assert(m.Read32(0x14020)==0x14028);
    assert(m.Read32(0x14028)==0x17000);
    assert(m.Read32(0x17024)==s.cpu.esp);
    assert(m.Read32(0x17028)==s.bootstrap.tls_data);
    assert(m.Read32(0x1712c)==1);
    assert(s.cpu.esp==s.bootstrap.initial_esp-4);
    assert(s.cpu.fs_base==0x14000 && s.cpu.thread_handle==1);
    assert(s.cpu.fcw==0x037f && s.cpu.mxcsr==0x1f80);
    assert(m.Read32(0x13000)==0xFE000044);
    assert(m.Read32(0x13004)==0x16000);
    XbeKernelPool pool(m,0x2000000,static_cast<uint32_t>(m.PageSize()));
    auto& cpu=s.cpu;
    uint32_t sp=s.bootstrap.initial_esp-16;
    cpu.esp=sp;
    m.Write32(sp,0x15000);
    m.Write32(sp+4,32);
    m.Write32(sp+8,0x1234);
    assert(DispatchPoolCall(15,cpu,m,pool));
    assert(cpu.esp==sp+12 && cpu.eax==0x2000000);
    auto allocated=cpu.eax;
    cpu.esp=sp;
    m.Write32(sp+4,allocated);
    assert(DispatchPoolCall(17,cpu,m,pool));
    assert(cpu.esp==sp+8 && pool.Outstanding()==0);
    cpu.esp=sp;
    m.Write32(sp+4,16);
    assert(DispatchPoolCall(14,cpu,m,pool));
    assert(cpu.eax==allocated && cpu.esp==sp+8);
    assert(!DispatchPoolCall(399,cpu,m,pool));
    XbeKernelSession session(m,0x2100000,static_cast<uint32_t>(m.PageSize()));
    cpu.esp=sp;
    m.Write32(sp+4,64);
    auto entry=+[](X86Context* c,uint8_t* base,uint32_t address,XbeKernelSession::Callback call)->int {
        assert(address==0x15000);
        call(*c,base,15);
        return 42;
    };
    assert(session.Run(entry,cpu,0x15000)==42);
    assert(cpu.eax==0x2100000 && cpu.esp==sp+12);
    auto bad=+[](X86Context* c,uint8_t* base,uint32_t,XbeKernelSession::Callback call)->int {
        call(*c,base,399);
        return 0;
    };
    rejected=false;
    try { session.Run(bad,cpu,0x15000); }
    catch(const std::runtime_error&) { rejected=true; }
    assert(rejected);
    cpu.esp=sp;
    assert(session.Run(entry,cpu,0x15000)==42);
    assert(cpu.eax==0x2100040);
}
