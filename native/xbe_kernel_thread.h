#pragma once
#include "xbox_guest_address_space.h"
#include <array>
#include <stdexcept>
namespace hulkpocket {
// Layout reference: Cxbx-Reloaded src/core/kernel/common/types.h
// blob 7da7fbc24a1ca3b84b2305752af741aa0feb5552.
// Diagnostic single thread only; scheduler/APC state is not implemented.
inline void InitializeKernelThread(XboxGuestAddressSpace& mem,uint32_t pcr,
    uint32_t thread,uint32_t stack_top,uint32_t stack_bottom,
    uint32_t esp,uint32_t tls,uint32_t handle) {
    if(!pcr || !thread || !handle || pcr%4 || thread%4 ||
       !mem.IsCommitted(pcr,0x280) || !mem.IsCommitted(thread,0x140) ||
       (uint64_t(pcr)<uint64_t(thread)+0x140 && uint64_t(thread)<uint64_t(pcr)+0x280) ||
       stack_bottom>=stack_top || esp<stack_bottom || esp>=stack_top ||
       !mem.IsCommitted(stack_bottom,stack_top-stack_bottom) ||
       (tls && !mem.IsCommitted(tls,1)))
        throw std::invalid_argument("Invalid diagnostic thread storage");
    std::array<uint8_t,0x280> zero{};
    mem.Write(pcr,zero.data(),zero.size());
    mem.Write(thread,zero.data(),0x140);
    mem.Write32(pcr,0xffffffff);
    mem.Write32(pcr+4,stack_top);
    mem.Write32(pcr+8,stack_bottom);
    mem.Write32(pcr+0x18,pcr);
    mem.Write32(pcr+0x1c,pcr);
    mem.Write32(pcr+0x20,pcr+0x28);
    mem.Write32(pcr+0x28,thread);
    mem.Write32(pcr+0x50,pcr+0x50);
    mem.Write32(pcr+0x54,pcr+0x50);
    mem.Write32(thread+0x10,thread+0x10);
    mem.Write32(thread+0x14,thread+0x10);
    mem.Write32(thread+0x1c,stack_top);
    mem.Write32(thread+0x20,stack_bottom);
    mem.Write32(thread+0x24,esp);
    mem.Write32(thread+0x28,tls);
    mem.Write32(thread+0x12c,handle);
}
}
