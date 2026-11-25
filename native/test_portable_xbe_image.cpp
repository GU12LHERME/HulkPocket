#include "portable_xbe_image.h"
#include <cstdint>
#include <cstdio>
#include <stdexcept>

int main(int argc,char** argv) {
    if(argc!=2) return 1;
    hulkpocket::PortableXbeImage image(argv[1]);
    if(image.Entry()!=0x300010 || image.CodeLo()!=0x300000 ||
       image.CodeHi()!=0x310000 || image.MappingCount()!=2) return 2;
    auto dispatcher=image.CreateDispatcher();
    X86Context ctx{};
    std::uint8_t guest[4]={7,0,0,0};
    {
        hulkpocket::ScopedXbeDispatch scope(*dispatcher);
        x86_icall(ctx,guest,image.Entry());
        if(ctx.eax!=0x123B || ctx.last_icall!=0x300010) return 3;
        x86_icall(ctx,guest,0x300020);
        if(ctx.ebx!=0xCAFE) return 4;
        bool rejected=false;
        try { x86_icall(ctx,guest,0x300030); }
        catch(const std::out_of_range&) { rejected=true; }
        if(!rejected) return 5;
    }
    std::puts("ELF dlopen SDK exports -> portable x86_icall -> native mapped code: passed");
    return 0;
}
