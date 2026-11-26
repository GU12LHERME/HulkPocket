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
}
