#include "hulk_kernel_data_registry.h"
#include "hulk_kernel_clock.h"
#include "hulk_kernel_profile.h"
#include "hulk_kernel_objects.h"
#include <cassert>
#include <cstdio>
using namespace hulkpocket;
int main() {
    XboxGuestAddressSpace mem;
    LoadedXbeImage image{};
    image.base=0x10000; image.image_size=0x10000; image.kernel_thunk=0x10000;
    mem.Commit(image.base, image.image_size);
    mem.Write32(0x10000,0x80000011);
    mem.Write32(0x10004,0x80000012);
    mem.Write32(0x10008,0);
    bool rejected=false;
    try {
        BindXbeKernelImports(image,mem,[](uint32_t n) {
            return n==17 ? KernelExportBinding{KernelExportKind::Function,0}
                         : KernelExportBinding{};
        });
    } catch(const std::runtime_error&) { rejected=true; }
    assert(rejected && mem.Read32(0x10000)==0x80000011);
    assert(mem.Read32(0x10004)==0x80000012);
    BindXbeKernelImports(image,mem,[](uint32_t n) {
        return n==17 ? KernelExportBinding{KernelExportKind::Function,0}
                     : KernelExportBinding{KernelExportKind::Data,0x11000};
    });
    assert(mem.Read32(0x10000)==0xFE000044);
    assert(mem.Read32(0x10004)==0x11000);
    assert(mem.Read32(0x10008)==0);
    unsigned functions=0,data=0;
    for(uint32_t n=0;n<400;++n) {
        auto kind=HulkKernelExportKind(n);
        functions += kind==KernelExportKind::Function;
        data += kind==KernelExportKind::Data;
    }
    assert(functions==141 && data==18);
    HulkKernelDataRegistry registry;
    XbeKernelImports imports;
    imports.complete=true;
    imports.ordinals={16,17,22};
    assert(registry.MissingData(imports).size()==2);
    registry.Register(16,0x12000,32,mem);
    assert(registry.Resolve(16).guest_data==0x12000);
    assert(registry.Resolve(17).kind==KernelExportKind::Function);
    assert(registry.MissingData(imports)==std::vector<uint32_t>{22});
    rejected=false;
    try { registry.Register(22,0x12004,32,mem); }
    catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    registry.Register(22,0x12100,32,mem);
    assert(registry.MissingData(imports).empty());
    assert(registry.Resolve(399).kind==KernelExportKind::Unknown);
    HulkKernelDataRegistry data_registry;
    HulkKernelClock clock(mem,data_registry,0x14000,10000);
    assert(mem.Read32(0x14004)==10000);
    clock.Update(mem);
    RegisterKernelProfile(mem,data_registry,0x14100,{1,0,5838,0,3,0x12,0x34,5,6});
    assert(mem.Read32(0x14100)==3);
    assert(mem.Read32(0x14104)==0x3412);
    assert(mem.Read32(0x14108)==1);
    assert(mem.Read32(0x1410c)==5838);
    assert(mem.Read32(0x14110)==5 && mem.Read32(0x14114)==6);
    unsigned n=0;
    for(uint32_t ordinal:{16u,22u,30u,31u,71u,259u}) {
        uint32_t addr=0x14200+32*n++;
        RegisterObjectType(mem,data_registry,ordinal,addr,{{0xFE000044,0xFE000048,0,0,0,0},0x12345678});
        assert(data_registry.Resolve(ordinal).guest_data==addr);
        assert(mem.Read32(addr)==0xFE000044 && mem.Read32(addr+24)==0x12345678);
    }
    XbeKernelImports all_data;
    all_data.complete=true;
    for(uint32_t ordinal=0;ordinal<400;++ordinal)
        if(HulkKernelExportKind(ordinal)==KernelExportKind::Data)
            all_data.ordinals.push_back(ordinal);
    assert(data_registry.MissingData(all_data).size()==6);
    std::puts("Kernel binding synthetic function/data and rejection tests passed");
}
