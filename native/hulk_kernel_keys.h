#pragma once
#include "hulk_kernel_data_registry.h"
#include <array>
namespace hulkpocket {
using XboxKeyBytes=std::array<uint8_t,16>;
// Caller supplies private, legally obtained key material. No embedded keys.
inline void RegisterKernelKeys(XboxGuestAddressSpace& mem,
    HulkKernelDataRegistry& registry,uint32_t base,
    const XboxKeyBytes& hd,const XboxKeyBytes& signature,
    const std::vector<XboxKeyBytes>& alternate) {
    if(alternate.empty() || alternate.size()>16 || base%4 ||
       !mem.IsCommitted(base,32+16*alternate.size()))
        throw std::invalid_argument("Invalid key storage");
    auto staged=registry;
    staged.Register(323,base,16,mem);
    staged.Register(325,base+16,16,mem);
    staged.Register(354,base+32,16*alternate.size(),mem);
    mem.Write(base,hd.data(),16);
    mem.Write(base+16,signature.data(),16);
    for(size_t i=0;i<alternate.size();++i)
        mem.Write(base+32+16*i,alternate[i].data(),16);
    registry=staged;
}
}
