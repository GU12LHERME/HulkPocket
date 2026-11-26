#pragma once
// HulkPocket bridge for the portable graphics converters, not the full XDK runtime.
#include "../../../native/xbox_guest_address_space.h"
#include <stdexcept>
namespace gpu {
inline thread_local hulkpocket::XboxGuestAddressSpace* g_guest_memory=nullptr;
class ScopedGuestMemory {
    hulkpocket::XboxGuestAddressSpace* previous_;
public:
    explicit ScopedGuestMemory(hulkpocket::XboxGuestAddressSpace& memory)
        :previous_(g_guest_memory){g_guest_memory=&memory;}
    ~ScopedGuestMemory(){g_guest_memory=previous_;}
    ScopedGuestMemory(const ScopedGuestMemory&)=delete;
    ScopedGuestMemory& operator=(const ScopedGuestMemory&)=delete;
};
inline uint8_t* CheckedGuestRange(uint64_t address,uint64_t bytes) {
    if(!g_guest_memory || address>=hulkpocket::XboxGuestAddressSpace::kSize ||
       bytes>hulkpocket::XboxGuestAddressSpace::kSize-address ||
       !g_guest_memory->IsCommitted(static_cast<uint32_t>(address),static_cast<size_t>(bytes)))
        throw std::out_of_range("GPU access outside committed guest memory");
    return g_guest_memory->Base()+address;
}
inline uint32_t PhysicalResourceAddress(uint32_t pa) {
    return pa<0x04000000u?0x80000000u|pa:pa;
}
inline uint32_t ReadGuest32(uint64_t address) {
    CheckedGuestRange(address,4);
    return g_guest_memory->Read32(static_cast<uint32_t>(address));
}
}
