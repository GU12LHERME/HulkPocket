#pragma once
#include "xbe_initial_cpu.h"
#include "xbe_tls_index.h"
#include "hulk_kernel_data_registry.h"
namespace hulkpocket {
// Single-use diagnostic session. Discard the address space on failure.
// Kernel data and KPCR must be prepared by the caller, not fabricated.
struct XbeEntrySession {
    XbeBootstrapState bootstrap;
    X86Context cpu;
};
inline XbeEntrySession PrepareEntrySession(
    const LoadedXbeImage& image, XboxGuestAddressSpace& memory,
    const HulkKernelDataRegistry& registry,
    uint32_t arena_base,uint32_t arena_end,
    uint32_t kpcr,uint32_t thread_handle,uint32_t return_address) {
    auto imports=ScanXbeKernelImports(image,memory);
    auto missing=registry.MissingData(imports);
    if(!missing.empty())
        throw std::runtime_error("Missing kernel data ordinal "+std::to_string(missing.front()));
    for(auto ordinal:imports.ordinals)
        if(registry.Resolve(ordinal).kind==KernelExportKind::Unknown)
            throw std::runtime_error("Unknown kernel ordinal "+std::to_string(ordinal));
    auto boot=PrepareXbeBootstrap(image,memory,arena_base,arena_end);
    InitializeDiagnosticTlsIndex(boot,memory);
    auto cpu=PrepareInitialCpu(boot,memory,kpcr,thread_handle,return_address);
    BindXbeKernelImports(image,memory,[&](uint32_t ordinal){return registry.Resolve(ordinal);});
    return {boot,cpu};
}
}
