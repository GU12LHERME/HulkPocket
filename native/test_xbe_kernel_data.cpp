#include "hulk_kernel_clock.h"
#include "hulk_kernel_profile.h"
#include "hulk_kernel_launch_data.h"
#include <cassert>
#include <cstring>
using namespace hulkpocket;
int main() {
    XboxGuestAddressSpace m;
    m.Commit(0x10000,0x10000);
    HulkKernelDataRegistry r;
    HulkKernelClock clock(m,r,0x10000,10000);
    assert(m.Read32(0x10004)==10000);
    clock.Update(m);
    RegisterKernelProfile(m,r,0x10100,{1,0,5838,0,3,4,5,2,1});
    assert(m.Read32(0x10100)==3 && m.Read32(0x10104)==0x504);
    assert(m.Read32(0x1010c)==5838);
    assert(r.Resolve(324).address==0x10108);
    RegisterLaunchData(m,r,0x10200,0,"D:\\default.xbe");
    assert(m.Read32(0x10200)==0);
    assert(m.Read32(0x10208)==0x1020c);
    char name[15]{};
    m.Read(0x1020c,name,15);
    assert(std::strcmp(name,"D:\\default.xbe")==0);
    HulkKernelDataRegistry conflict;
    conflict.Register(157,0x10300,4,m);
    bool rejected=false;
    try { HulkKernelClock bad(m,conflict,0x10400,10000); }
    catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected && conflict.Resolve(156).kind==KernelExportKind::Unknown);
}
