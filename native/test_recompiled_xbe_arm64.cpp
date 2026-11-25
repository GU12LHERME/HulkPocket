#include "portable_xbe_dispatch.h"
#include "xbe_image_memory_loader.h"
#include "xbox_guest_address_space.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

// This test links the real C++ instruction bodies emitted by
// XboxXBERecomp into an actual Linux AArch64 executable.
// It is run under qemu-aarch64, NOT x86 host emulation.
extern "C" {
extern const std::uint32_t X86CodeBase;
extern const std::uint32_t X86CodeEnd;
extern const std::uint32_t X86EntryPoint;
extern const X86FuncMapping X86FuncMappings[];
}
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    std::ifstream f(argv[1],std::ios::binary);
    if(!f) return 2;
    const std::vector<std::uint8_t> data(
        (std::istreambuf_iterator<char>(f)),{});
    hulkpocket::XboxGuestAddressSpace memory;
    const auto original=hulkpocket::LoadXbeSections(data,memory);
    if(original.entry!=X86EntryPoint || X86EntryPoint!=0x200000u ||
       X86CodeBase!=0x200000u || X86CodeEnd!=0x200100u)
        return 3;
    std::size_t count=0;
    while(count<1024 && X86FuncMappings[count].host)++count;
    if(!count || count==1024)return 4;
    hulkpocket::PortableXbeDispatch dispatch(
        X86FuncMappings,count,X86CodeBase,X86CodeEnd);
    memory.Commit(0x50000,4096);
    memory.Write32(0x50000,0);
    X86Context context{};
    context.esp=0x50000;
    {
        hulkpocket::ScopedXbeDispatch scope(dispatch);
        x86_icall(context,memory.Base(),original.entry);
    }
    if(context.eax!=0x12345678u ||
       context.last_icall!=original.entry)
        return 5;
    std::puts("Original synthetic x86 bytes -> SDK recompiler -> native AArch64 machine execution: passed");
    return 0;
}
