#include "xbe_image_memory_loader.h"
#include "xbe_resident_sections.h"
#include "xbe_kernel_imports.h"
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace hulkpocket;
namespace {
void Store(std::vector<std::uint8_t>& d,std::size_t o,std::uint32_t n) {
    if(o+4>d.size()) throw std::out_of_range("test XBE write");
    for(std::size_t i=0;i<4;++i) d[o+i]=std::uint8_t(n>>(8*i));
}
std::vector<std::uint8_t> MakeXbe() {
    std::vector<std::uint8_t> image(0x900,0);
    image[0]='X'; image[1]='B'; image[2]='E'; image[3]='H';
    constexpr std::uint32_t base=0x10000;
    Store(image,0x104,base);
    Store(image,0x108,0x400);
    Store(image,0x10C,0x6000);
    Store(image,0x118,base+0x180);
    Store(image,0x11C,2);
    Store(image,0x120,base+0x240);
    Store(image,0x128,0x12000u ^ 0xA8FC57ABu);
    Store(image,0x158,0x13000u ^ 0x5B6D40B6u);
    Store(image,0x12C,0x22000u);
    Store(image,0x130,0x10000u);
    Store(image,0x134,0x90000u);
    Store(image,0x138,0x20000u);
    Store(image,0x124,0x12345678u);
    Store(image,0x188,0x56550039);
    // 56-byte section table entry #0.
    Store(image,0x240,6); // executable + preloaded
    Store(image,0x240+4,0x12000);
    Store(image,0x240+8,0x1000);
    Store(image,0x240+12,0x600);
    Store(image,0x240+16,16);
    // section #1, with BSS suffix.
    Store(image,0x278+4,0x13000);
    Store(image,0x278+8,0x1000);
    Store(image,0x278+12,0x700);
    Store(image,0x278+16,8);
    for(unsigned i=0;i<16;++i) image[0x600+i]=std::uint8_t(i+1);
    for(unsigned i=0;i<8;++i) image[0x700+i]=std::uint8_t(0xA0+i);
    return image;
}
bool Rejected(const std::vector<std::uint8_t>& bytes) {
    XboxGuestAddressSpace address_space;
    try { (void)LoadXbeSections(bytes,address_space); }
    catch(const std::invalid_argument&) {return true;}
    return false;
}
}
int main() {
    const auto image=MakeXbe();
    XboxGuestAddressSpace memory;
    const auto loaded=LoadXbeSections(image,memory);
    if(loaded.base!=0x10000 || loaded.image_size!=0x6000 ||
       loaded.entry!=0x12000 || loaded.kernel_thunk!=0x13000 ||
       loaded.title_id!=0x56550039 || loaded.sections!=2 ||
       loaded.tls_va!=0x22000u || loaded.stack_commit!=0x10000u ||
       loaded.heap_reserve!=0x90000u || loaded.heap_commit!=0x20000u ||
       loaded.init_flags!=0x12345678u)
        return 1;
    if(memory.Read32(0x12000)!=0x04030201 ||
       memory.Read32(0x13000)!=0xA3A2A1A0 ||
       memory.Read32(0x12010)!=0 ||
       memory.Read32(0x13008)!=0 ||
       memory.Read32(0x10000)!=0x48454258)
        return 2;
    if(memory.IsCommitted(0x11000,4) ||
       !memory.IsCommitted(0x12000,0x1000))
        return 3;
    XbeResidentSections sections(memory,loaded,image);
    if(memory.Read32(0x10258)!=1||memory.Read32(0x10290)!=0)return 30;
    if(sections.Change(0x10278,true)||memory.Read32(0x10290)!=1||
       memory.Read32(0x13000)!=0xA3A2A1A0)return 31;
    if(sections.Change(0x10278,true)||memory.Read32(0x10290)!=2)return 32;
    if(sections.Change(0x10278,false)||sections.Change(0x10278,false)||
       memory.Read32(0x10290)!=0||!memory.IsCommitted(0x13000,0x1000))return 33;
    if(sections.Change(0x10278,false)!=0xc000000du||
       sections.Change(0x1027c,true)!=0xc000000du)return 34;
    memory.Write32(0x1027c,0x14000);
    if(sections.Change(0x10278,true)!=0xc000000du)return 35;
    memory.Write32(0x1027c,0x13000);
    memory.Write32(0x10290,0xffffffffu);
    if(sections.Change(0x10278,true)!=0xc000000du)return 36;
    memory.Write32(0x10290,0);
    if(sections.Change(0x10240,true)||memory.Read32(0x10258)!=2)return 37;
    // Imported Xbox kernel APIs are encoded as 0x80000000|ordinal
    // and zero terminated in the original XBE before relocation.
    memory.Write32(0x13000,0x80000011u);
    memory.Write32(0x13004,0x80000025u);
    memory.Write32(0x13008,0x80000011u);
    memory.Write32(0x1300C,0);
    auto imports = ScanXbeKernelImports(loaded,memory);
    if(!imports.complete || imports.entries!=3 ||
       imports.ordinals!=std::vector<std::uint32_t>({17,37}) ||
       !imports.error.empty()) return 20;
    if(ScanXbeKernelImports(loaded,memory,2).complete) return 21;
    memory.Write32(0x13004,0xFE000044u);
    if(ScanXbeKernelImports(loaded,memory).complete) return 22;
    memory.Write32(0x13004,0x800003FFu);
    if(ScanXbeKernelImports(loaded,memory).complete) return 23;
    memory.Write32(0x13004,0x80000025u);

    {
        auto bad=image;
        Store(bad,0x240+12,0xFFFF);
        if(!Rejected(bad)) return 4; // raw beyond physical XBE
    }
    {
        auto bad=image;
        Store(bad,0x278+4,0x12004);
        if(!Rejected(bad)) return 5; // virtual section collision
    }
    {
        auto bad=image;
        Store(bad,0x11C,5000);
        if(!Rejected(bad)) return 6; // section count out of bounds
    }
    {
        auto bad=image;
        Store(bad,0x128,0);
        if(!Rejected(bad)) return 7; // invalid entry XOR key
    }
    {
        auto bad=image;
        Store(bad,0x240+8,8);
        if(!Rejected(bad)) return 8; // physical larger than virtual
    }
    {
        auto bad=image;
        Store(bad,0x240+4,0x10200);
        if(!Rejected(bad)) return 9; // header overlap
    }
    std::puts("Checked Xbox XBE header + sections + virtual BSS load into ARM64 guest memory: passed");
    return 0;
}
