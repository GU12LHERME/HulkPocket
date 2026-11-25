#pragma once
#include "native_fiber.h"
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>

namespace hulkpocket {

// Host-side cooperative dispatcher for original Xbox guest fiber IDs.
// Every guest executes on its own native AArch64 stack. SwitchTo behaves
// like a suspend: it returns only after another fiber switches back to it.
// Does not read/write guest TLS or XBE structures; that adapter comes later.
class XboxFiberScheduler {
public:
    using Entry = NativeFiber::Entry;
    XboxFiberScheduler();
    XboxFiberScheduler(const XboxFiberScheduler&) = delete;
    XboxFiberScheduler& operator=(const XboxFiberScheduler&) = delete;
    ~XboxFiberScheduler();

    void Create(std::uint32_t id, Entry entry, void* user,
                std::size_t stack_bytes = 256 * 1024);
    // Host dispatch runs until a fiber returns or explicitly yields to host.
    void Run(std::uint32_t starting_id);
    // Only callable within an active guest fiber on the creator thread.
    void SwitchTo(std::uint32_t target_id);
    void YieldToHost();
    bool Exists(std::uint32_t id) const;
    bool Finished(std::uint32_t id) const;
    std::uint32_t CurrentId() const noexcept { return current_; }
private:
    void CheckOwner() const;
    std::unordered_map<std::uint32_t, std::unique_ptr<NativeFiber>> fibers_;
    std::thread::id owner_;
    std::uint32_t current_ = 0;
    std::uint32_t requested_ = 0;
    bool dispatching_ = false;
};
}
