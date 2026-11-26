#pragma once
#include "hulk_kernel_data_registry.h"
#include <chrono>
namespace hulkpocket {
// Xbox exports 156 (volatile DWORD) and 157 (const ULONG).
// Interval is supplied by the runtime in units of 100 ns.
class HulkKernelClock {
    using Clock=std::chrono::steady_clock;
    Clock::time_point start_;
    uint32_t tick_, increment_, interval_;
public:
    HulkKernelClock(XboxGuestAddressSpace& mem,HulkKernelDataRegistry& registry,
                    uint32_t address,uint32_t interval)
        :start_(Clock::now()),tick_(address),increment_(address+4),interval_(interval) {
        if(!interval || address>0xfffffff8u || address%4 ||
           !mem.IsCommitted(address,8))
            throw std::invalid_argument("Invalid kernel clock storage");
        registry.Register(156,tick_,4,mem);
        registry.Register(157,increment_,4,mem);
        mem.Write32(tick_,0);
        mem.Write32(increment_,interval_);
    }
    void Update(XboxGuestAddressSpace& mem) const {
        auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start_).count();
        mem.Write32(tick_,static_cast<uint32_t>(ns/100/interval_));
    }
};
}
