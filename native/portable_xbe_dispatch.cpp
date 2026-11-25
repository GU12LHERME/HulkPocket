#include "portable_xbe_dispatch.h"
#include <algorithm>
#include <stdexcept>

namespace hulkpocket {
namespace {
thread_local const PortableXbeDispatch* active_dispatch = nullptr;
}

PortableXbeDispatch::PortableXbeDispatch(
    const X86FuncMapping* mappings, std::size_t count,
    std::uint32_t code_lo, std::uint32_t code_hi, KernelCall kernel)
    : owner_(std::this_thread::get_id()), lo_(code_lo), hi_(code_hi),
      kernel_(kernel) {
    if (!code_lo || code_hi <= code_lo || (count && !mappings))
        throw std::invalid_argument("invalid Xbox code mapping");
    if (count > 1000000)
        throw std::length_error("unreasonably large Xbox mapping table");
    mappings_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& map = mappings[i];
        if (!map.host || map.guest < lo_ || map.guest >= hi_)
            throw std::invalid_argument("guest mapping outside executable region");
        mappings_.push_back(map);
    }
    std::sort(mappings_.begin(), mappings_.end(),
              [](const X86FuncMapping& a, const X86FuncMapping& b) {
                  return a.guest < b.guest;
              });
    for (std::size_t i = 1; i < mappings_.size(); ++i) {
        if (mappings_[i - 1].guest == mappings_[i].guest)
            throw std::invalid_argument("duplicate Xbox function address");
    }
}

bool PortableXbeDispatch::Contains(std::uint32_t target) const {
    const auto it = std::lower_bound(
        mappings_.begin(), mappings_.end(), target,
        [](const X86FuncMapping& m, std::uint32_t v) { return m.guest < v; });
    return it != mappings_.end() && it->guest == target;
}

void PortableXbeDispatch::Call(
    X86Context& ctx, std::uint8_t* base, std::uint32_t target) const {
    if (std::this_thread::get_id() != owner_)
        throw std::logic_error("Xbox guest dispatch called from foreign host thread");
    if (!base) throw std::invalid_argument("null Xbox guest memory base");
    // XboxXBERecomp dispatch.cpp reserves FE000000+ordinal*4 for
    // Xbox kernel imports. The portable runtime supplies the kernel
    // implementation separately from the recompiled game mappings.
    constexpr std::uint32_t kernel_base = 0xFE000000;
    constexpr std::uint32_t kernel_entries = 400;
    if (target >= kernel_base &&
        target < kernel_base + kernel_entries * 4) {
        if (!kernel_ || (target - kernel_base) % 4)
            throw std::out_of_range("unimplemented Xbox kernel thunk");
        ctx.last_icall = target;
        kernel_(ctx, base, (target - kernel_base) / 4);
        return;
    }
    const auto it = std::lower_bound(
        mappings_.begin(), mappings_.end(), target,
        [](const X86FuncMapping& m, std::uint32_t v) { return m.guest < v; });
    if (it == mappings_.end() || it->guest != target)
        throw std::out_of_range("unmapped Xbox guest entry; code was not recompiled");
    ctx.last_icall = target;
    it->host(ctx, base);
}

ScopedXbeDispatch::ScopedXbeDispatch(const PortableXbeDispatch& dispatcher)
    : previous_(active_dispatch) {
    active_dispatch = &dispatcher;
}
ScopedXbeDispatch::~ScopedXbeDispatch() {
    active_dispatch = previous_;
}
} // namespace hulkpocket

void x86_icall(X86Context& ctx, std::uint8_t* base, std::uint32_t target) {
    // Bound by ScopedXbeDispatch on the owning guest thread. The ARM64
    // fiber scheduler is cooperative and stays on that same thread.
    if (!hulkpocket::active_dispatch)
        throw std::logic_error("no Xbox guest code dispatcher bound");
    hulkpocket::active_dispatch->Call(ctx, base, target);
}
