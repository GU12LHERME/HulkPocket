#pragma once
#include "hulk_kernel_data_registry.h"
namespace hulkpocket {
struct HulkKernelProfile {
    uint16_t major,minor,build,qfe;
    uint32_t hardware_flags;
    uint8_t gpu_revision,mcp_revision;
    uint32_t disk_cache_partitions,boot_video_mode;
};
// Layouts from nxdk xboxkrnl.h; caller chooses verified profile values.
// No console secrets or fabricated object-type structures.
inline void RegisterKernelProfile(XboxGuestAddressSpace& mem,
    HulkKernelDataRegistry& registry,uint32_t base,const HulkKernelProfile& p) {
    if(base%4 || base>0xffffffe8u || !mem.IsCommitted(base,24))
        throw std::invalid_argument("Invalid kernel profile storage");
    auto staged=registry;
    staged.Register(322,base,8,mem);
    staged.Register(324,base+8,8,mem);
    staged.Register(40,base+16,4,mem);
    staged.Register(356,base+20,4,mem);
    mem.Write32(base,p.hardware_flags);
    mem.Write32(base+4,uint32_t(p.gpu_revision)|(uint32_t(p.mcp_revision)<<8));
    mem.Write32(base+8,uint32_t(p.major)|(uint32_t(p.minor)<<16));
    mem.Write32(base+12,uint32_t(p.build)|(uint32_t(p.qfe)<<16));
    mem.Write32(base+16,p.disk_cache_partitions);
    mem.Write32(base+20,p.boot_video_mode);
    registry=staged;
}
}
