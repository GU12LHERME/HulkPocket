#pragma once
#include "hulk_kernel_data_registry.h"
#include <array>
namespace hulkpocket {
// nxdk OBJECT_TYPE: six 32-bit guest pointers followed by ULONG PoolTag.
struct HulkObjectType {
    std::array<uint32_t,6> pointers;
    uint32_t pool_tag;
};
inline void RegisterObjectType(XboxGuestAddressSpace& mem,
    HulkKernelDataRegistry& registry,uint32_t ordinal,uint32_t address,
    const HulkObjectType& type) {
    switch(ordinal) {
        case 16: case 22: case 30: case 31: case 71: case 259: break;
        default: throw std::invalid_argument("Not an Xbox object type");
    }
    if(address%4 || !mem.IsCommitted(address,28))
        throw std::invalid_argument("Invalid object type storage");
    // Allocation and free callbacks are mandatory; other callbacks are optional.
    if(!type.pointers[0] || !type.pointers[1])
        throw std::invalid_argument("Missing object allocator callbacks");
    registry.Register(ordinal,address,28,mem);
    for(unsigned i=0;i<6;++i) mem.Write32(address+i*4,type.pointers[i]);
    mem.Write32(address+24,type.pool_tag);
}
 // Cxbx-Reloaded EmuKrnlEx.cpp / EmuKrnlPs.cpp: object type
 // descriptors for dispatcher objects; callbacks use our kernel thunk ABI.
 inline void RegisterDispatcherObjectTypes(XboxGuestAddressSpace& mem,
     HulkKernelDataRegistry& registry,uint32_t base) {
     constexpr std::array<uint32_t,5> ordinals{16,22,30,31,259};
     constexpr std::array<uint32_t,5> tags{0x76657645,0x6174754d,0x616d6553,0x656d6954,0x65726854};
     if(base%64 || uint64_t(base)+5*64>XboxGuestAddressSpace::kSize ||
        !mem.IsCommitted(base,5*64))
         throw std::invalid_argument("Invalid dispatcher type arena");
     for(unsigned i=0;i<ordinals.size();++i) {
         RegisterObjectType(mem,registry,ordinals[i],base+i*64,
             {{0xfe000000u+15*4,0xfe000000u+17*4,0,0,0,0},tags[i]});
     }
 }
 // Only the empty IDE device queue used by XAPI is supported.
 // Match XboxXBERecomp InitKernelData: DeviceListHead at offset 0x28.
 inline void RegisterDiagnosticIdeQueue(XboxGuestAddressSpace& mem,
     HulkKernelDataRegistry& registry,uint32_t address) {
     if(address%4 || !mem.IsCommitted(address,64))
         throw std::invalid_argument("Invalid IDE queue storage");
     registry.Register(357,address,64,mem);
     std::array<uint8_t,64> zero{};
     mem.Write(address,zero.data(),zero.size());
     mem.Write32(address+0x28,address+0x28);
     mem.Write32(address+0x2c,address+0x28);
 }
}
