#include "xbe_bootstrap.h"
#include "xbe_tls_index.h"
#include <cassert>
using namespace hulkpocket;
int main() {
    XboxGuestAddressSpace m;
    m.Commit(0x10000,0x10000);
    LoadedXbeImage i{};
    i.base=0x10000;i.image_size=0x10000;i.entry=0x15000;
    i.stack_commit=0x10000;i.heap_reserve=0x100000;i.heap_commit=0x1000;
    i.tls_va=0x10000;
    m.Write32(0x10000,0x11000);m.Write32(0x10004,0x11004);
    m.Write32(0x10008,0x12000);m.Write32(0x10010,8);
    m.Write32(0x11000,0xabcdef01);
    auto s=PrepareXbeBootstrap(i,m,0x1000000,0x1200000);
    assert(s.entry==i.entry && s.initial_esp<s.tls_data);
    assert(s.tls_size==12 && m.Read32(s.tls_data)==0xabcdef01);
    assert(m.Read32(s.tls_data+4)==0);
    assert(!m.IsCommitted(s.memory.guard,1));
    assert(!m.IsCommitted(s.memory.heap_base+s.memory.heap_committed,1));
    m.Write32(s.tls.index,0xffffffff);
    InitializeDiagnosticTlsIndex(s,m);
    assert(s.memory.stack_top+4u*m.Read32(s.tls.index)==s.tls_data);
    assert(s.tls_data==s.memory.stack_top-20);
    assert((s.initial_esp-4)%16==0);
    auto bad=s;
    bad.tls.callbacks=0x13000;
    bool callback_rejected=false;
    try { InitializeDiagnosticTlsIndex(bad,m); }
    catch(const std::runtime_error&) { callback_rejected=true; }
    assert(callback_rejected);
    bool rejected=false;
    try { PrepareXbeBootstrap(i,m,0x1000000,0x1200000); }
    catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
}
