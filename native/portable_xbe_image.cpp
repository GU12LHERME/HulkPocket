#include "portable_xbe_image.h"
#include <dlfcn.h>
#include <stdexcept>

namespace hulkpocket {
namespace {
template <typename T>
const T* Symbol(void* handle, const char* name) {
    dlerror();
    void* found = dlsym(handle, name);
    if (dlerror() || !found)
        throw std::runtime_error(std::string("missing ELF Xbox export: ") + name);
    return static_cast<const T*>(found);
}
}
PortableXbeImage::PortableXbeImage(const std::string& elf_path) {
    if (elf_path.empty()) throw std::invalid_argument("empty guest ELF path");
    void* handle = dlopen(elf_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* err = dlerror();
        throw std::runtime_error(err ? err : "cannot load Xbox ELF image");
    }
    try {
        const auto* lo = Symbol<std::uint32_t>(handle,"X86CodeBase");
        const auto* hi = Symbol<std::uint32_t>(handle,"X86CodeEnd");
        const auto* entry = Symbol<std::uint32_t>(handle,"X86EntryPoint");
        const auto* mappings = Symbol<X86FuncMapping>(handle,"X86FuncMappings");
        if (!*lo || *lo >= *hi || *entry < *lo || *entry >= *hi)
            throw std::invalid_argument("invalid ELF Xbox code address range");
        // XboxXBERecomp terminates X86FuncMappings with a null host
        // entry. Scan only the advertised guest code byte span and
        // impose a hard upper bound on hostile/broken generated files.
        // The module is trusted generated code; an untrusted ELF must
        // NEVER be dlopened because it executes code during loading.
        const std::size_t limit =
            (*hi - *lo) < 1000000u ? (*hi - *lo) : 1000000u;
        std::size_t n=0;
        while (n < limit && mappings[n].host) ++n;
        if (n == limit || !n)
            throw std::invalid_argument("missing or empty ELF guest mapping terminator");
        handle_=handle;
        mappings_=mappings; count_=n;
        lo_=*lo; hi_=*hi; entry_=*entry;
    } catch (...) {
        dlclose(handle);
        throw;
    }
}
PortableXbeImage::KernelEntry PortableXbeImage::ResolveKernelEntry() const {
    dlerror();
    void* symbol=dlsym(handle_,"hulk_entry_execute_with_kernel");
    if(dlerror() || !symbol)
        throw std::runtime_error("ELF lacks kernel-enabled entry; relink cached module");
    return reinterpret_cast<KernelEntry>(symbol);
}
PortableXbeImage::~PortableXbeImage() {
    if (handle_) dlclose(handle_);
}
std::unique_ptr<PortableXbeDispatch> PortableXbeImage::CreateDispatcher(
    PortableXbeDispatch::KernelCall kernel) const {
    return std::make_unique<PortableXbeDispatch>(
        mappings_,count_,lo_,hi_,kernel);
}
}
