#pragma once
#include "xbe_image_memory_loader.h"
#include <vector>
#include <stdexcept>
namespace hulkpocket {
struct XbeTlsTemplate {
    std::uint32_t start=0,end=0,index=0,callbacks=0,zero_fill=0,characteristics=0;
};
inline XbeTlsTemplate ReadXbeTls(const LoadedXbeImage& image,const XboxGuestAddressSpace& mem) {
    if (!image.tls_va) return {};
    if (!mem.IsCommitted(image.tls_va,24)) throw std::invalid_argument("unmapped TLS directory");
    auto a=image.tls_va;
    XbeTlsTemplate t{mem.Read32(a),mem.Read32(a+4),mem.Read32(a+8),mem.Read32(a+12),mem.Read32(a+16),mem.Read32(a+20)};
    if(t.end<t.start || std::uint64_t(t.end)-t.start+t.zero_fill>16*1024*1024 ||
       !mem.IsCommitted(t.start,t.end-t.start) ||
       (t.index && !mem.IsCommitted(t.index,4)))
        throw std::invalid_argument("invalid TLS template");
    return t;
}
// Copies only TLS data; does not invoke callbacks, set FS, or assign an index.
inline std::size_t CopyXbeTls(const XbeTlsTemplate& t,XboxGuestAddressSpace& mem,std::uint32_t dest) {
    if(t.end<t.start || std::uint64_t(t.end)-t.start+t.zero_fill>16*1024*1024)
        throw std::invalid_argument("invalid TLS size");
    const auto raw=t.end-t.start;
    const std::size_t total=std::size_t(raw)+t.zero_fill;
    if(!mem.IsCommitted(dest,total)) throw std::invalid_argument("TLS destination not committed");
    std::vector<std::uint8_t> bytes(total,0);
    if(raw) mem.Read(t.start,bytes.data(),raw);
    if(total) mem.Write(dest,bytes.data(),total);
    return total;
}
}
