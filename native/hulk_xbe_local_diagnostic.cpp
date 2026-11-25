#include "hulk_xbe_preflight.h"
#include "xbe_image_memory_loader.h"
#include "xbox_guest_address_space.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Runs locally: no XBE data is copied to GitHub or printed.
// Supplying an ELF module calls dlopen; only use modules built yourself
// from your own game and trusted source code.
int main(int argc,char** argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: hulk_xbe_local_diagnostic <original.xbe> [trusted-recompiled-arm64.so]\n";
        return 2;
    }
    try {
        std::ifstream file(argv[1],std::ios::binary|std::ios::ate);
        if(!file) throw std::runtime_error("cannot open local XBE");
        const auto length=file.tellg();
        if(length < 0 || static_cast<std::uint64_t>(length)>512u*1024u*1024u)
            throw std::runtime_error("XBE file exceeds local validation size cap");
        const auto bytes=static_cast<std::size_t>(length);
        file.seekg(0);
        std::vector<std::uint8_t> data(bytes);
        if(bytes && !file.read(reinterpret_cast<char*>(data.data()),
                               static_cast<std::streamsize>(bytes)))
            throw std::runtime_error("cannot read complete XBE");
        hulkpocket::XboxGuestAddressSpace guest;
        const auto loaded=hulkpocket::LoadXbeSections(data,guest);
        std::cout << "XBE parsed and mapped: title=0x"
                  << std::uppercase << std::hex << loaded.title_id
                  << ", entry=0x" << loaded.entry
                  << ", kernel_thunk=0x" << loaded.kernel_thunk
                  << std::dec << ", sections=" << loaded.sections << '\n';
        if(argc == 3) {
            hulkpocket::PortableXbeImage native(argv[2]);
            const auto result=hulkpocket::CheckHulkXbePreflight(loaded,native);
            std::cout << "Module match: " << (result.ReadyForGuestCall() ? "PASS" : "REJECT")
                      << ", candidate_region=" << result.region_candidate
                      << ", mapped_functions=" << native.MappingCount() << '\n';
            if(!result.ReadyForGuestCall()) {
                std::cerr << "Reason: " << result.failure << '\n';
                return 3;
            }
        } else {
            std::cout << "No ELF supplied: native function mapping NOT checked.\n";
        }
        std::cout << "Preflight only: no Xbox instructions, graphics or audio executed.\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "XBE local diagnostic failed: " << ex.what() << '\n';
        return 1;
    }
}
