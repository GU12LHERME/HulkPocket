#pragma once
#include <cstdint>
#include <cstddef>
#include <deque>
namespace hulkpocket {
// Bounded completion FIFO, called under the kernel session lock.
// ABI reference: Hulk Recompiled 6603e8a patches/z-kernel-io.patch (MIT).
struct XbeIoPacket { uint32_t key,context,status,information; };
struct XbeIoCompletion {
    static constexpr std::size_t capacity=256;
    std::deque<XbeIoPacket> packets;
    bool Full()const{return packets.size()>=capacity;}
};
}
