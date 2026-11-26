#pragma once
#include "portable_xbe_dispatch.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace hulkpocket {

// Loads an Android/Linux ELF shared object with the SAME generated
// XboxXBERecomp export names as the original Windows DLL.
// The guest code itself must already have been compiled as AArch64
// for Android; dlopen cannot execute Windows PE or x86 machine code.
class PortableXbeImage {
public:
    explicit PortableXbeImage(const std::string& elf_path);
    ~PortableXbeImage();
    PortableXbeImage(const PortableXbeImage&) = delete;
    PortableXbeImage& operator=(const PortableXbeImage&) = delete;
    std::uint32_t Entry() const noexcept { return entry_; }
    std::uint32_t CodeLo() const noexcept { return lo_; }
    std::uint32_t CodeHi() const noexcept { return hi_; }
    std::size_t MappingCount() const noexcept { return count_; }
    std::unique_ptr<PortableXbeDispatch> CreateDispatcher(
        PortableXbeDispatch::KernelCall kernel = nullptr) const;
    using KernelEntry = int (*)(X86Context*, std::uint8_t*, std::uint32_t,
                                PortableXbeDispatch::KernelCall);
    KernelEntry ResolveKernelEntry() const;
private:
    void* handle_ = nullptr;
    const X86FuncMapping* mappings_ = nullptr;
    std::size_t count_ = 0;
    std::uint32_t lo_ = 0, hi_ = 0, entry_ = 0;
};
}
