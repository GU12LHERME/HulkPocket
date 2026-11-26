#include "xbe_kernel_imports.h"
#include <algorithm>
#include <set>

namespace hulkpocket {
XbeKernelImports ScanXbeKernelImports(
    const LoadedXbeImage& image, const XboxGuestAddressSpace& memory,
    std::size_t max_entries) {
    XbeKernelImports report;
    const std::uint64_t image_end =
        std::uint64_t(image.base) + image.image_size;
    const std::uint64_t at = image.kernel_thunk;
    if (at < image.base || at >= image_end || (at & 3u) != 0) {
        report.error = "kernel import table outside XBE image or unaligned";
        return report;
    }
    if (!max_entries || max_entries > 4096u) {
        report.error = "invalid maximum kernel import entries";
        return report;
    }
    std::set<std::uint32_t> seen;
    for (std::size_t i = 0; i < max_entries; ++i) {
        const std::uint64_t pos = at + std::uint64_t(i) * 4u;
        if (pos + 4u > image_end || pos > UINT32_MAX ||
            !memory.IsCommitted(static_cast<std::uint32_t>(pos), 4u)) {
            report.error = "kernel import table extends beyond committed XBE image";
            return report;
        }
        const auto value = memory.Read32(static_cast<std::uint32_t>(pos));
        if (value == 0u) {
            report.complete = true;
            return report;
        }
        // Original Xbox import descriptors use 0x80000000|ordinal.
        // Retained patched FE000000 addresses are not legal on-disk
        // originals; do not silently interpret them as ordinals.
        if ((value & 0xFFFF0000u) != 0x80000000u) {
            report.error = "unexpected original Xbox kernel thunk entry";
            return report;
        }
        const auto ordinal = value & 0xFFFFu;
        if (ordinal >= 400u) {
            report.error = "XBE imports unsupported kernel ordinal";
            return report;
        }
        ++report.entries;
        if (seen.insert(ordinal).second)
            report.ordinals.push_back(ordinal);
    }
    report.error = "kernel import table not terminated within entry limit";
    return report;
}
} // namespace hulkpocket
