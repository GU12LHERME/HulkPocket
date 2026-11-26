#pragma once
#include "xbe_startup_memory.h"
#include "xbe_tls.h"
namespace hulkpocket {
struct XbeBootstrapState {
    XbeStartupLayout memory;
    XbeTlsTemplate tls;
    std::uint32_t entry, initial_esp, tls_data;
    std::size_t tls_size;
};
// Prepares memory and entry metadata only. No guest dispatch, FS/KPCR,
// TLS index assignment or callbacks. Caller owns arena exclusively.
inline XbeBootstrapState PrepareXbeBootstrap(
    const LoadedXbeImage& image, XboxGuestAddressSpace& mem,
    std::uint32_t base,std::uint32_t end) {
    if (!image.entry || !mem.IsCommitted(image.entry,1))
        throw std::invalid_argument("unmapped guest entry");
    const auto tls=ReadXbeTls(image,mem);
    const std::size_t size=std::size_t(tls.end-tls.start)+tls.zero_fill;
    const uint64_t aligned=(uint64_t(size)+3)&~uint64_t(3);
    auto expanded=image;
    const uint64_t stack=uint64_t(image.stack_commit)+aligned+32;
    if(stack>0xffffffffu) throw std::invalid_argument("TLS stack overflow");
    expanded.stack_commit=static_cast<uint32_t>(stack);
    const auto layout=InitializeXbeStartupMemory(expanded,mem,base,end);
    const auto dest=layout.stack_top-static_cast<uint32_t>(aligned);
    if(size) CopyXbeTls(tls,mem,dest);
    const auto esp=(dest&~15u)-12;
    return {layout,tls,image.entry,esp,size?dest:0,size};
}
}
