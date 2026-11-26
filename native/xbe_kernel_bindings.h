#pragma once
#include "xbe_kernel_imports.h"
#include <stdexcept>
#include <vector>

namespace hulkpocket {
enum class KernelExportKind { Unknown, Function, Data };
struct KernelExportBinding {
    KernelExportKind kind = KernelExportKind::Unknown;
    uint32_t guest_data = 0;
};
// Resolver must use a verified Xbox export inventory. Unknown exports
// fail before any table mutation. Data exports must point to committed
// guest storage, never to a native host pointer.
template<class Resolve>
void BindXbeKernelImports(const LoadedXbeImage& image,
                          XboxGuestAddressSpace& memory, Resolve resolve) {
    const auto report = ScanXbeKernelImports(image, memory);
    if (!report.complete) throw std::runtime_error(report.error);
    std::vector<uint32_t> replacements;
    replacements.reserve(report.entries);
    for (size_t i=0; i<report.entries; ++i) {
        const uint32_t slot = image.kernel_thunk + uint32_t(i*4);
        const uint32_t ordinal = memory.Read32(slot) & 0xffffu;
        const auto binding = resolve(ordinal);
        if (binding.kind == KernelExportKind::Function && ordinal != 0)
            replacements.push_back(0xFE000000u + ordinal*4u);
        else if (binding.kind == KernelExportKind::Data &&
                 binding.guest_data && memory.IsCommitted(binding.guest_data, 4))
            replacements.push_back(binding.guest_data);
        else
            throw std::runtime_error("Unresolved Xbox kernel export ordinal " + std::to_string(ordinal));
    }
    for (size_t i=0; i<replacements.size(); ++i)
        memory.Write32(image.kernel_thunk + uint32_t(i*4), replacements[i]);
}
}
