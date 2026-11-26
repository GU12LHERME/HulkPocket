#include "xbe_kernel_bindings.h"
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
    std::puts("Kernel binding synthetic function/data and rejection tests passed");
}
