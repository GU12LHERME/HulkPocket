#include "xbox_fiber_scheduler.h"
#include <stdexcept>
#include <exception>

namespace hulkpocket {
XboxFiberScheduler::XboxFiberScheduler() : owner_(std::this_thread::get_id()) {}
XboxFiberScheduler::~XboxFiberScheduler() {
    if (dispatching_) std::terminate();
}
void XboxFiberScheduler::CheckOwner() const {
    if (owner_ != std::this_thread::get_id())
        throw std::logic_error("Xbox fiber dispatcher called from another thread");
}
void XboxFiberScheduler::Create(std::uint32_t id, Entry entry, void* user, std::size_t stack_bytes) {
    CheckOwner();
    // XDK CreateFiber occurs while another fiber is running. Allow a
    // newly initialized guest native stack to be registered from that
    // same active execution, but never from an unrelated thread.
    if ((dispatching_ && current_ == 0) || id == 0 || fibers_.count(id))
        throw std::logic_error("invalid or duplicate guest fiber id");
    auto fiber = std::make_unique<NativeFiber>(entry, user, stack_bytes);
    fibers_.emplace(id, std::move(fiber));
}
bool XboxFiberScheduler::Exists(std::uint32_t id) const {
    CheckOwner();
    return fibers_.find(id) != fibers_.end();
}
bool XboxFiberScheduler::Finished(std::uint32_t id) const {
    CheckOwner();
    auto it = fibers_.find(id);
    if (it == fibers_.end()) throw std::out_of_range("unknown guest fiber id");
    return it->second->Finished();
}
void XboxFiberScheduler::Run(std::uint32_t starting_id) {
    CheckOwner();
    if (dispatching_ || !Exists(starting_id) || Finished(starting_id))
        throw std::logic_error("cannot start guest fiber dispatcher");
    dispatching_ = true;
    requested_ = 0;
    std::uint32_t next = starting_id;
    try {
        while (next) {
            auto it = fibers_.find(next);
            if (it == fibers_.end() || it->second->Finished())
                throw std::logic_error("requested guest fiber does not exist or has finished");
            current_ = next;
            requested_ = 0;
            const bool suspended = it->second->Resume();
            current_ = 0;
            if (!suspended) break;
            next = requested_;
        }
    } catch (...) {
        current_ = requested_ = 0;
        dispatching_ = false;
        throw;
    }
    current_ = requested_ = 0;
    dispatching_ = false;
}
void XboxFiberScheduler::SwitchTo(std::uint32_t target_id) {
    CheckOwner();
    if (!dispatching_ || !current_ || !target_id ||
        target_id == current_ || !Exists(target_id) || Finished(target_id))
        throw std::logic_error("invalid guest fiber switch");
    requested_ = target_id;
    NativeFiber::Yield();
}
void XboxFiberScheduler::YieldToHost() {
    CheckOwner();
    if (!dispatching_ || !current_)
        throw std::logic_error("yield to host outside active guest fiber");
    requested_ = 0;
    NativeFiber::Yield();
}
}
