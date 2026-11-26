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
    const auto page=mem.PageSize();
    const auto layout=PlanXbeStartupLayout(image,base,end,static_cast<std::uint32_t>(page));
    const std::size_t size=std::size_t(tls.end-tls.start)+tls.zero_fill;
    const std::uint64_t dest=std::uint64_t(layout.heap_base)+layout.heap_reserved;
    const std::uint64_t rounded=(size+page-1)&~std::uint64_t(page-1);
    if(dest+rounded>end) throw std::invalid_argument("insufficient TLS arena");
    InitializeXbeStartupMemory(image,mem,base,end);
    if(size) {
        mem.Commit(static_cast<std::uint32_t>(dest),size);
        CopyXbeTls(tls,mem,static_cast<std::uint32_t>(dest));
    }
    return {layout,tls,image.entry,layout.stack_top,
        size?static_cast<std::uint32_t>(dest):0,size};
}
}
