#include "xbe_tls.h"
#include <cassert>
using namespace hulkpocket;
int main() {
    XboxGuestAddressSpace m;
    m.Commit(0x10000,0x10000);
    LoadedXbeImage i{};
    assert(ReadXbeTls(i,m).start==0);
    i.tls_va=0x10000;
    m.Write32(0x10000,0x11000);
    m.Write32(0x10004,0x11004);
    m.Write32(0x10008,0x12000);
    m.Write32(0x10010,8);
    m.Write32(0x11000,0x12345678);
    m.Write32(0x13004,0xffffffff);
    auto t=ReadXbeTls(i,m);
    assert(CopyXbeTls(t,m,0x13000)==12);
    assert(m.Read32(0x13000)==0x12345678);
    assert(m.Read32(0x13004)==0);
    assert(m.Read32(0x13008)==0);
    bool rejected=false;
    m.Write32(0x10004,0x10000);
    try { ReadXbeTls(i,m); } catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    rejected=false;
    try { CopyXbeTls(t,m,0x30000); } catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
}
