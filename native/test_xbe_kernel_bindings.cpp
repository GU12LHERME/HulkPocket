#include "hulk_kernel_data_registry.h"
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
    std::puts("Kernel binding synthetic function/data and rejection tests passed");
}
