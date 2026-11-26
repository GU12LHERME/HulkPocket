#pragma once
#include "xbox_fiber_scheduler.h"
#include "cpu/x86_context.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace hulkpocket {

// This supplies native stack ownership and guest-code invocation for
// newly-created Xbox fibers. The actual x86_icall runtime function is
// provided by the embedding SDK host; it is never linked into the test.
class XbeGuestFiberBootstrap {
public:
    using GuestCall = void (*)(X86Context&, std::uint8_t*, std::uint32_t);
    XbeGuestFiberBootstrap(
        XboxFiberScheduler& scheduler, X86Context& context,
        std::uint8_t* base, GuestCall call,
        bool trap_on_return=false)
        : scheduler_(scheduler), context_(context), base_(base), call_(call),
          trap_on_return_(trap_on_return) {
        if (!base || !call) throw std::invalid_argument("missing guest runtime");
    }

    void Register(std::uint32_t id, std::uint32_t entry,
                  std::size_t stack_bytes = 256 * 1024) {
        if (!id || !entry || records_.count(id))
            throw std::invalid_argument("invalid or repeated guest fiber");
        auto item = std::make_unique<Record>(Record{this, id, entry});
        // Register first: Create can fail without leaving a dangling
        // callback pointer; the caller owns the bootstrap until all
        // suspended fibers are finished/destroyed.
        scheduler_.Create(id, &Start, item.get(), stack_bytes);
        records_.emplace(id, std::move(item));
    }

    void Delete(std::uint32_t id) {
        auto it=records_.find(id);
        if(it==records_.end())throw std::logic_error("unknown guest bootstrap fiber");
        scheduler_.Delete(id);
        records_.erase(it);
    }
    bool UnexpectedReturn(std::uint32_t id) const {
        auto it = records_.find(id);
        return it != records_.end() && it->second->returned;
    }

private:
    struct Record {
        XbeGuestFiberBootstrap* self;
        std::uint32_t id;
        std::uint32_t entry;
        bool returned = false;
    };
    static void Start(void* raw) {
        auto& item = *static_cast<Record*>(raw);
        auto& self = *item.self;
        if (self.scheduler_.CurrentId() != item.id)
            throw std::logic_error("guest fiber bootstrap on wrong native stack");
        self.call_(self.context_, self.base_, item.entry);
        // The real Windows Hulk port traps here: Xbox bootstrap must not
        // return normally. Keep this in the test as a recorded failure
        // state; production runtime must raise an Xbox guest trap.
        item.returned = true;
        if(self.trap_on_return_)throw std::runtime_error("Xbox fiber bootstrap returned unexpectedly");
    }
    XboxFiberScheduler& scheduler_;
    X86Context& context_;
    std::uint8_t* base_;
    GuestCall call_;
    bool trap_on_return_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Record>> records_;
};
}
