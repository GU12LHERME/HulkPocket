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

int main(int argc,char** argv) {
    if(argc!=2)return 1;
    std::ifstream file(argv[1],std::ios::binary);
    if(!file)return 2;
    const std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(file)),{});
    hulkpocket::XboxGuestAddressSpace mem;
    const auto xbe=hulkpocket::LoadXbeSections(raw,mem);
    if(xbe.entry!=X86EntryPoint)return 3;
    std::size_t n=0;
    while(n<256 && X86FuncMappings[n].host) ++n;
    if(n<2 || n==256)return 4; // both caller and callee must be recompiled
    hulkpocket::PortableXbeDispatch dispatcher(X86FuncMappings,n,X86CodeBase,X86CodeEnd);
    if(!dispatcher.Contains(xbe.entry) || !dispatcher.Contains(xbe.entry+11))
        return 5;
    mem.Commit(0x50000,4096);
    const std::uint32_t esp=0x50F00;
    mem.Write32(esp,0); // synthetic host return
    X86Context ctx{};
    ctx.esp=esp;
    {
        hulkpocket::ScopedXbeDispatch scope(dispatcher);
        x86_icall(ctx,mem.Base(),xbe.entry);
    }
    if(ctx.eax!=12 || ctx.esp!=esp) {
        std::fprintf(stderr,
            "CALL/RET guest state mismatch: eax=%08X expected=0000000C; esp=%08X expected=%08X; stack=%08X\\n",
            ctx.eax,ctx.esp,esp,mem.Read32(esp));
        return 6;
    }
    std::puts("Original x86 caller->callee->return stack semantics recompiled to ARM64: passed");
    return 0;
}
