#pragma once
#include "aarch64_context.h"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <thread>

namespace hulkpocket {

// Single-thread cooperative fiber, backed by a guarded mmap stack. No guest
// Xbox records are referenced here. This is an ARM64 host runtime primitive.
class NativeFiber {
public:
    using Entry = void (*)(void*);
    explicit NativeFiber(Entry entry, void* user, std::size_t stack_bytes = 256 * 1024);
    ~NativeFiber();
    NativeFiber(const NativeFiber&) = delete;
    NativeFiber& operator=(const NativeFiber&) = delete;

    // Resume from creator thread. Returns false when the entry has finished.
    bool Resume();
    // Yield from the running entry, returning to the most recent Resume().
    static void Yield();
    bool Finished() const noexcept { return finished_; }
private:
    [[noreturn]] static void Bootstrap();
    Aarch64Context caller_{}, guest_{};
    Entry entry_;
    void* user_;
    void* mapping_ = nullptr;
    std::size_t mapping_bytes_ = 0;
    bool running_ = false;
    bool finished_ = false;
    std::exception_ptr error_{};
    std::thread::id owner_;
    static thread_local NativeFiber* active_;
};
}
