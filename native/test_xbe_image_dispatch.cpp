#include "portable_xbe_image.h"
#include "xbox_guest_address_space.h"
#include "xbe_image_memory_loader.h"
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void Write(std::vector<std::uint8_t>& x,std::size_t offset,std::uint32_t v) {
    if(offset+4>x.size()) throw std::out_of_range("synthetic test data");
    for(int i=0;i<4;++i) x[offset+i]=std::uint8_t(v>>(i*8));
}
std::vector<std::uint8_t> ExampleXbe() {
    std::vector<std::uint8_t> x(0x800,0);
    x[0]='X';x[1]='B';x[2]='E';x[3]='H';
    constexpr std::uint32_t base=0x10000;
    Write(x,0x104,base);
    Write(x,0x108,0x400);
    Write(x,0x10C,0x310000);
    Write(x,0x118,base+0x180);
    Write(x,0x11C,1);
    Write(x,0x120,base+0x240);
    Write(x,0x128,0x300010u ^ 0xA8FC57ABu);
    Write(x,0x158,0x300020u ^ 0x5B6D40B6u);
    Write(x,0x188,0x56550039u);
    Write(x,0x240+4,0x300000);
    Write(x,0x240+8,0x1000);
    Write(x,0x240+12,0x600);
    Write(x,0x240+16,0x100);
    Write(x,0x600+0x50,0x87654321u);
    return x;
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    hulkpocket::XboxGuestAddressSpace mem;
    const auto loaded=hulkpocket::LoadXbeSections(ExampleXbe(),mem);
    hulkpocket::PortableXbeImage module(argv[1]);
    if(loaded.entry!=module.Entry() || loaded.title_id!=0x56550039u ||
       !mem.IsCommitted(0x300050,4))return 2;
    auto dispatch=module.CreateDispatcher();
    X86Context ctx{};
    ctx.ecx=0x300050;
    {
        hulkpocket::ScopedXbeDispatch bind(*dispatch);
        x86_icall(ctx,mem.Base(),loaded.entry);
    }
    if(ctx.eax!=0x87654321u || ctx.last_icall!=loaded.entry) return 3;
    std::puts("Synthetic XBE sections -> real 4 GiB guest memory -> SDK ELF function map -> x86_icall: passed");
    return 0;
}
