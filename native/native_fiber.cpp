#include "native_fiber.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace hulkpocket {
thread_local NativeFiber* NativeFiber::active_ = nullptr;

NativeFiber::NativeFiber(Entry entry, void* user, std::size_t stack_bytes)
    : entry_(entry), user_(user), owner_(std::this_thread::get_id()) {
    if (!entry || stack_bytes < 64 * 1024 || stack_bytes > 64 * 1024 * 1024)
        throw std::invalid_argument("invalid native fiber entry or stack size");
    const long page_len = sysconf(_SC_PAGESIZE);
    if (page_len <= 0 || (page_len & (page_len - 1)) != 0)
        throw std::runtime_error("unsupported page size");
    const std::size_t page = std::size_t(page_len);
    const std::size_t aligned = (stack_bytes + page - 1) & ~(page - 1);
    mapping_bytes_ = aligned + page * 2;
    mapping_ = mmap(nullptr, mapping_bytes_, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        throw std::runtime_error("fiber stack allocation failed");
    }
    auto* stack = static_cast<char*>(mapping_) + page;
    if (mprotect(stack, aligned, PROT_READ | PROT_WRITE) != 0) {
        munmap(mapping_, mapping_bytes_);
        mapping_ = nullptr;
        throw std::runtime_error("fiber stack protection failed");
    }
    guest_.sp = reinterpret_cast<std::uintptr_t>(stack + aligned) & ~std::uintptr_t(15);
    guest_.gpr[11] = reinterpret_cast<std::uintptr_t>(&Bootstrap);
}

NativeFiber::~NativeFiber() {
    // Destroying the currently running stack would use-after-free the stack.
    if (running_ || active_ == this) std::abort();
    if (mapping_) munmap(mapping_, mapping_bytes_);
}

bool NativeFiber::Resume() {
    if (owner_ != std::this_thread::get_id() || running_ || active_)
        throw std::logic_error("fiber resume requires creator thread and no active fiber");
    if (finished_) return false;
    active_ = this;
    running_ = true;
    hulkpocket_aarch64_switch(&caller_, &guest_);
    running_ = false;
    active_ = nullptr;
    return !finished_;
}

void NativeFiber::Yield() {
    NativeFiber* fiber = active_;
    if (!fiber || fiber->owner_ != std::this_thread::get_id() || !fiber->running_)
        throw std::logic_error("fiber yield outside native fiber");
    hulkpocket_aarch64_switch(&fiber->guest_, &fiber->caller_);
}

[[noreturn]] void NativeFiber::Bootstrap() {
    NativeFiber* fiber = active_;
    if (!fiber || !fiber->running_) std::abort();
    try {
        fiber->entry_(fiber->user_);
    } catch (...) {
        // Do not unwind a C++ exception across an assembly context switch.
        std::abort();
    }
    fiber->finished_ = true;
    hulkpocket_aarch64_switch(&fiber->guest_, &fiber->caller_);
    std::abort();
}
}
