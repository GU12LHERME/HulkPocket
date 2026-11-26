#include "hulk_kernel_clock.h"
#include "hulk_kernel_profile.h"
#include "hulk_kernel_objects.h"
#include "xbe_kernel_physical_memory.h"
#include "hulk_kernel_launch_data.h"
#include <cassert>
#include <cstring>
using namespace hulkpocket;
int main() {
    XboxGuestAddressSpace m;
    m.Commit(0x10000,0x10000);
    InitializeXboxKernelPhysicalRegion(m);
    assert(m.IsCommitted(0x8001003c,4) && m.Read32(0x8001003c)==0);
    assert(m.IsCommitted(0x800ffffc,4) && !m.IsCommitted(0x80100000,1));
    XbePhysicalMemory physical(m);
    assert(physical.Allocate(4096,0,0xfffff,4096)==0);
    assert(!physical.Free(0x80010000));
    bool duplicate=false;
    try { InitializeXboxKernelPhysicalRegion(m); } catch(const std::runtime_error&) { duplicate=true; }
    assert(duplicate);
    HulkKernelDataRegistry r;
    HulkKernelClock clock(m,r,0x10000,10000);
    assert(m.Read32(0x10004)==10000);
    clock.Update(m);
    RegisterKernelProfile(m,r,0x10100,{1,0,5838,0,3,4,5,2,1});
    assert(m.Read32(0x10100)==3 && m.Read32(0x10104)==0x504);
    assert(m.Read32(0x1010c)==5838);
    assert(r.Resolve(324).guest_data==0x10108);
    RegisterLaunchData(m,r,0x10200,0,"D:\\default.xbe");
    assert(m.Read32(0x10200)==0);
    assert(m.Read32(0x10208)==0x1020c);
    char name[15]{};
    m.Read(0x1020c,name,15);
    assert(std::strcmp(name,"D:\\default.xbe")==0);
    HulkKernelDataRegistry retail;
    RegisterKernelProfile(m,retail,0x10300,kRetailDiagnosticProfile);
    assert(retail.Resolve(322).guest_data==0x10300);
    assert(m.Read32(0x10300)==0x20 && m.Read32(0x10304)==0xb1d3);
    assert(m.Read32(0x10308)==1 && m.Read32(0x1030c)==(5838u|0x10000u));
    assert(m.Read32(0x10310)==3 && m.Read32(0x10314)==0);
    RegisterDispatcherObjectTypes(m,retail,0x10400);
    RegisterDiagnosticIdeQueue(m,retail,0x10600);
    const auto ev=retail.Resolve(16).guest_data;
    assert(ev==0x10400 && m.Read32(ev)==0xfe00003c && m.Read32(ev+4)==0xfe000044);
    assert(m.Read32(ev+24)==0x76657645);
    assert(retail.Resolve(259).guest_data==0x10500);
    assert(m.Read32(0x10628)==0x10628 && m.Read32(0x1062c)==0x10628);
    HulkKernelDataRegistry conflict;
    conflict.Register(157,0x10300,4,m);
    bool rejected=false;
    try { HulkKernelClock bad(m,conflict,0x10400,10000); }
    catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected && conflict.Resolve(156).kind==KernelExportKind::Unknown);
}
