#pragma once
#include "hulk_kernel_data_registry.h"
#include <string>
namespace hulkpocket {
// Xbox ANSI_STRING is two USHORT lengths followed by a 32-bit guest pointer.
// A zero launch page pointer explicitly represents no launch payload.
inline void RegisterLaunchData(XboxGuestAddressSpace& mem,
    HulkKernelDataRegistry& registry,uint32_t base,uint32_t launch_page,
    const std::string& image_name) {
    if(image_name.empty() || image_name.size()>65534 ||
       image_name.find('\0')!=std::string::npos || base%4 ||
       uint64_t(base)+13+image_name.size()>XboxGuestAddressSpace::kSize ||
       !mem.IsCommitted(base,13+image_name.size()) ||
       (launch_page && !mem.IsCommitted(launch_page,4096)))
        throw std::invalid_argument("Invalid launch data");
    auto staged=registry;
    staged.Register(164,base,4,mem);
    staged.Register(326,base+4,8,mem);
    mem.Write32(base,launch_page);
    auto length=static_cast<uint32_t>(image_name.size());
    mem.Write32(base+4,length|((length+1)<<16));
    mem.Write32(base+8,base+12);
    mem.Write(base+12,image_name.c_str(),length+1);
    registry=staged;
}
}
