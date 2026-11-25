#include "portable_xbe_dispatch.h"
#include "xbe_image_memory_loader.h"
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
int main(int argc, char** argv) {
    if(argc!=2) return 1;
    std::ifstream file(argv[1],std::ios::binary);
    if(!file) return 2;
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    hulkpocket::XboxGuestAddressSpace memory;
    const auto image=hulkpocket::LoadXbeSections(bytes,memory);
    if(image.entry!=X86EntryPoint || !memory.IsCommitted(0x200084u,4u))
        return 3;
    std::size_t count=0;
    while(count<1000u && X86FuncMappings[count].host) ++count;
    if(!count || count==1000u) return 4;
    hulkpocket::PortableXbeDispatch dispatch(
        X86FuncMappings,count,X86CodeBase,X86CodeEnd);
    if(memory.Read32(0x200080u)!=0x12345670u ||
       memory.Read32(0x200084u)!=0)
        return 5;
    memory.Commit(0x50000u,4096u);
    memory.Write32(0x50000u,0u);
    X86Context ctx{};
    ctx.esp=0x50000u;
    {
        hulkpocket::ScopedXbeDispatch scope(dispatch);
        x86_icall(ctx,memory.Base(),image.entry);
    }
    if(ctx.eax!=0x12345677u ||
       memory.Read32(0x200084u)!=0x12345677u ||
       memory.Read32(0x200080u)!=0x12345670u)
        return 6;
    std::puts("Real x86 load/add/store instructions recompiled to native ARM64 with Xbox guest RAM: passed");
    return 0;
}
