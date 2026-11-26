#include "portable_xbe_dispatch.h"
#include "xbe_image_memory_loader.h"
#include "xbe_kernel_imports.h"
#include "xbox_guest_address_space.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" {
extern const std::uint32_t X86CodeBase;
extern const std::uint32_t X86CodeEnd;
extern const std::uint32_t X86EntryPoint;
extern const X86FuncMapping X86FuncMappings[];
}
namespace {
std::uint32_t observed_ordinal=0xFFFFFFFFu;
unsigned kernel_calls=0;
void KernelStub(X86Context& ctx,std::uint8_t*,std::uint32_t ordinal) {
    observed_ordinal=ordinal;
    ++kernel_calls;
    ctx.eax=0xCAFEu;
    // The test's no-argument kernel service returns to the guest
    // caller by popping the original x86 CALL return address.
    ctx.esp+=4u;
}
}
int main(int argc,char** argv) {
    if(argc!=2) return 1;
    std::ifstream file(argv[1],std::ios::binary);
    if(!file) return 2;
    const std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(file)), {});
    hulkpocket::XboxGuestAddressSpace memory;
    const auto original=hulkpocket::LoadXbeSections(bytes,memory);
    const auto imports=hulkpocket::ScanXbeKernelImports(original,memory);
    if(!imports.complete || imports.ordinals != std::vector<std::uint32_t>({17u}))
        return 3;
    std::size_t count=0;
    while(count<128u && X86FuncMappings[count].host)++count;
    if(count!=1u || X86EntryPoint!=original.entry) return 4;
    hulkpocket::PortableXbeDispatch dispatcher(
        X86FuncMappings,count,X86CodeBase,X86CodeEnd,KernelStub);
    memory.Commit(0x50000u,4096u);
    const auto esp=0x50F00u;
    memory.Write32(esp,0);
    X86Context ctx{};
    ctx.esp=esp;
    {
        hulkpocket::ScopedXbeDispatch scope(dispatcher);
        x86_icall(ctx,memory.Base(),original.entry);
    }
    if(kernel_calls!=1u || observed_ordinal!=17u ||
       ctx.eax!=0xCAFEu || ctx.esp!=esp+4u) {
        std::fprintf(stderr,
            "Xbox kernel call mismatch: calls=%u ordinal=%u eax=%08X esp=%08X expected=%08X\n",
            kernel_calls,observed_ordinal,ctx.eax,ctx.esp,esp+4u);
        return 5;
    }
    std::puts("Genuine x86 indirect Xbox kernel call -> recompiled ARM64 -> ordinal 17 host bridge: passed");
    return 0;
}
