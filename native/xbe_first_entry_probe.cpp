// Local probe: verifies dispatcher binding inside the same ELF as guest code.
// Does not execute guest code until Xbox TLS and kernel import setup are ready.
#include "portable_xbe_dispatch.h"
#include <cstdint>
#include <cstdio>
#include <exception>
extern "C" {
extern const uint32_t X86CodeBase, X86CodeEnd, X86EntryPoint;
extern const X86FuncMapping X86FuncMappings[];
__attribute__((visibility("default"))) int hulk_entry_probe(uint32_t expected) {
    try {
        if (expected != X86EntryPoint) return 2;
        size_t n=0;
        while(n<1000000 && X86FuncMappings[n].host) ++n;
        if(!n || n==1000000) return 3;
        hulkpocket::PortableXbeDispatch dispatch(X86FuncMappings,n,X86CodeBase,X86CodeEnd);
        hulkpocket::ScopedXbeDispatch scope(dispatch);
        if(!dispatch.Contains(expected)) return 4;
        std::fprintf(stderr,"In-module dispatcher ready: %zu functions, entry=%08X\n",n,expected);
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"Dispatcher rejected: %s\n",e.what());
        return 1;
    }
}
}

#include <stdexcept>
namespace {
void stop_kernel(X86Context&, uint8_t*, uint32_t ordinal) {
    std::fprintf(stderr,"Unsupported kernel ordinal: %u\n",ordinal);
    throw std::runtime_error("Kernel service not implemented");
}
}
// Trusted local caller must prepare the guest memory and CPU first.
// Run in an isolated diagnostic process: native faults are not C++ exceptions.
extern "C" __attribute__((visibility("default")))
int hulk_entry_execute_with_kernel(X86Context* cpu, uint8_t* base, uint32_t entry,
    hulkpocket::PortableXbeDispatch::KernelCall kernel) {
    if(!cpu || !base || !entry || !cpu->esp ||
       !cpu->fs_base || !cpu->thread_handle) return 2;
    try {
        size_t n=0;
        while(n<1000000 && X86FuncMappings[n].host) ++n;
        if(!n || n==1000000) return 3;
        hulkpocket::PortableXbeDispatch dispatch(
            X86FuncMappings,n,X86CodeBase,X86CodeEnd,kernel ? kernel : stop_kernel);
        hulkpocket::ScopedXbeDispatch scope(dispatch);
        std::fprintf(stderr,"Entering guest at %08X ESP=%08X\n",entry,cpu->esp);
        dispatch.Call(*cpu,base,entry);
        std::fprintf(stderr,"Guest entry returned\n");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"Guest stopped: %s; last indirect target=%08X\n",
                     e.what(),cpu->last_icall);
        return 1;
    }
}

extern "C" __attribute__((visibility("default")))
int hulk_entry_execute(X86Context* cpu, uint8_t* base, uint32_t entry) {
    return hulk_entry_execute_with_kernel(cpu,base,entry,nullptr);
}
