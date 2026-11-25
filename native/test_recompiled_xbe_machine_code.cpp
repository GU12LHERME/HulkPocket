#include "portable_xbe_image.h"
#include "xbe_image_memory_loader.h"
#include "xbox_guest_address_space.h"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

// Executes *actual recompiler output* from a synthetic original-Xbox
// XBE containing only mov eax, 0x12345678; ret. No Hulk game code.
int main(int argc,char** argv) {
    if(argc!=3) return 1;
    std::ifstream input(argv[1],std::ios::binary);
    if(!input) return 2;
    const std::vector<std::uint8_t> contents(
        (std::istreambuf_iterator<char>(input)),{});
    hulkpocket::XboxGuestAddressSpace memory;
    const auto original=hulkpocket::LoadXbeSections(contents,memory);
    hulkpocket::PortableXbeImage recompiled(argv[2]);
    if(original.entry!=0x200000u || recompiled.Entry()!=original.entry)
        return 3;
    auto dispatcher=recompiled.CreateDispatcher();
    if(!dispatcher->Contains(original.entry)) return 4;
    memory.Commit(0x50000,4096);
    memory.Write32(0x50000,0);
    X86Context context{};
    context.esp=0x50000;
    {
        hulkpocket::ScopedXbeDispatch scope(*dispatcher);
        x86_icall(context,memory.Base(),original.entry);
    }
    if(context.eax!=0x12345678u ||
       context.last_icall!=original.entry) {
        std::cerr<<"Recompiled x86 'mov eax, immediate' did not execute correctly\n";
        return 5;
    }
    std::cout<<"Real SDK x86 opcode translation -> native ELF -> x86_icall -> guest context: passed\n";
    return 0;
}
