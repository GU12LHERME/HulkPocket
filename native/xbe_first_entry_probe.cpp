// Local probe: verifies dispatcher binding inside the same ELF as guest code.
// Does not execute guest code until Xbox TLS and kernel import setup are ready.
#include "portable_xbe_dispatch.h"
#include "xbe_guest_thread_exit.h"
#include "xbe_graphics_boundary.h"
#if defined(__aarch64__) && __has_include("hulk_verified_fibers.h")
#include "hulk_verified_fibers.h"
#if HULK_VERIFIED_FIBERS
#include "hulk_live_fibers.h"
#endif
#endif
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>
#include <stdexcept>
#if __has_include("hulk_extra_mapping.h")
#include "hulk_extra_mapping.h"
#else
static const X86FuncMapping kHulkExtraMapping[]={{0,nullptr}};
#endif
extern "C" {
extern const uint32_t X86CodeBase, X86CodeEnd, X86EntryPoint;
extern const X86FuncMapping X86FuncMappings[];
}
namespace {
std::vector<X86FuncMapping> AllMappings(){
    size_t n=0;
    while(n<1000000&&X86FuncMappings[n].host)++n;
    if(!n||n==1000000)throw std::runtime_error("Invalid primary guest function map");
    std::vector<X86FuncMapping> all(X86FuncMappings,X86FuncMappings+n);
    for(const auto& extra:kHulkExtraMapping){
        if(!extra.host)break;
        if(all.size()>=1000000)throw std::runtime_error("Guest function map capacity exceeded");
        all.push_back(extra);
    }
    return all; // dispatcher validates ranges and rejects duplicates
}
}
extern "C" {
__attribute__((visibility("default"))) int hulk_entry_probe(uint32_t expected) {
    try {
        if (expected != X86EntryPoint) return 2;
        auto all=AllMappings();
        hulkpocket::PortableXbeDispatch dispatch(all.data(),all.size(),X86CodeBase,X86CodeEnd);
        hulkpocket::ScopedXbeDispatch scope(dispatch);
        if(!dispatch.Contains(expected)) return 4;
        std::fprintf(stderr,"In-module dispatcher ready: %zu functions, entry=%08X\n",all.size(),expected);
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
        auto all=AllMappings();
        hulkpocket::PortableXbeDispatch dispatch(
            all.data(),all.size(),X86CodeBase,X86CodeEnd,kernel ? kernel : stop_kernel);
        hulkpocket::ScopedXbeDispatch scope(dispatch);
        hulkpocket::ScopedGraphicsCallback graphics_scope(kernel);
        std::fprintf(stderr,"Entering guest at %08X ESP=%08X\n",entry,cpu->esp);
#if defined(HULK_VERIFIED_FIBERS) && HULK_VERIFIED_FIBERS
        if(kernel && !hulkpocket::HulkLiveFibers::Active()){
            hulkpocket::HulkLiveFibers fibers(*cpu,base,x86_icall,hulkpocket::kHulkUsaFiberLayout);
            fibers.Run(entry);
        }else dispatch.Call(*cpu,base,entry);
#else
        dispatch.Call(*cpu,base,entry);
#endif
        std::fprintf(stderr,"Guest entry returned\n");
        return 0;
    } catch(const hulkpocket::GuestThreadExit& e) {
        cpu->eax=e.code;
        std::fprintf(stderr,"Guest thread exited: %08X\n",e.code);
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
