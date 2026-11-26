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
