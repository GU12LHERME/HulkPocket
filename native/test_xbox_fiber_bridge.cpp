#include "xbox_fiber_bridge.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace hulkpocket;
namespace {
constexpr std::uint32_t a = 0x1000, b = 0x1100, tls = 0x1200, fs = 0x1300;
constexpr std::uint32_t stack_a = 0x8000, stack_b = 0x9000;
struct State {
    XboxFiberScheduler scheduler;
    XboxGuestFiberRegs regs{};
    std::vector<std::uint32_t> memory = std::vector<std::uint32_t>(65536);
    std::vector<int> trace;
    std::uint32_t read(std::uint32_t at) const {
        if ((at & 3) || at / 4 >= memory.size()) throw std::out_of_range("guest read");
        return memory[at / 4];
    }
    void write(std::uint32_t at, std::uint32_t value) {
        if ((at & 3) || at / 4 >= memory.size()) throw std::out_of_range("guest write");
        memory[at / 4] = value;
    }
    XboxGuestFiberSwap switch_to(std::uint32_t target) {
        XboxFiberBridge bridge(scheduler, regs);
        return bridge.SwitchTo(
            target, tls,
            [this](std::uint32_t at) { return read(at); },
            [this](std::uint32_t at, std::uint32_t val) { write(at, val); });
    }
};
void first(void* raw) {
    auto& state = *static_cast<State*>(raw);
    state.trace.push_back(1);
    if (state.scheduler.CurrentId() != a || state.read(tls + 0x10) != a)
        throw std::logic_error("invalid A startup");
    state.switch_to(b);
    state.trace.push_back(3);
    // B restored A's saved x86 frame before waking the *same*
    // native C++ continuation, not a newly-created C++ frame.
    if (state.read(tls + 0x10) != a || state.regs.ebx != 0x2001 ||
        state.regs.ebp != 0x2004 || state.regs.esp != stack_a + 8 ||
        state.read(fs) != 0x7777)
        throw std::logic_error("A guest register restore failed");
    state.switch_to(b);
    // Deliberately suspended here. No host code should restart the
    // original game's continuation until the other guest resumes it.
    state.trace.push_back(9);
}
void second(void* raw) {
    auto& state = *static_cast<State*>(raw);
    state.trace.push_back(2);
    if (state.scheduler.CurrentId() != b || state.read(tls + 0x10) != b ||
        state.regs.ebx != 0x1001 || state.regs.ebp != 0x1004 ||
        state.regs.esp != stack_b + 28 || state.read(fs) != 0x5555)
        throw std::logic_error("B initial guest frame restore failed");
    state.write(stack_a, 0x50EBD);
    state.write(stack_a + 4, 0xBEEF);
    state.switch_to(a);
    state.trace.push_back(4);
    if (state.read(tls + 0x10) != b || state.regs.ebx != 0x1001 ||
        state.regs.ebp != 0x1004 || state.read(fs) != 0x5555 ||
        state.regs.esp != stack_b + 36)
        throw std::logic_error("B resumed guest frame was lost");
    state.scheduler.YieldToHost();
    state.trace.push_back(5);
}
} // namespace

int main() {
    State state;
    state.regs.fs_base = fs;
    state.regs.esp = stack_a;
    state.regs.ebp = 0x2004;
    state.regs.esi = 0x2003;
    state.regs.edi = 0x2002;
    state.regs.ebx = 0x2001;
    state.write(fs, 0x7777);
    state.write(tls + 0x10, a);
    const std::array<std::uint32_t, 7> bframe = {
        0x5555, 0x1001, 0x1002, 0x1003, 0x1004, 0x50DD9, 0xDEAD
    };
    for (std::size_t i = 0; i < bframe.size(); ++i)
        state.write(stack_b + std::uint32_t(i * 4), bframe[i]);
    state.write(b + 0x0C, stack_b);
    state.scheduler.Create(a, first, &state);
    state.scheduler.Create(b, second, &state);
    state.scheduler.Run(a);
    if (state.trace != std::vector<int>({1, 2, 3, 4}) ||
        state.scheduler.CurrentId() != 0 ||
        state.read(tls + 0x10) != b ||
        state.scheduler.Finished(a) || state.scheduler.Finished(b))
        return 1;
    // B resumes on its own native stack without rerunning entry.
    state.scheduler.Run(b);
    if (state.trace != std::vector<int>({1, 2, 3, 4, 5}) ||
        !state.scheduler.Finished(b) || state.scheduler.Finished(a))
        return 2;
    std::puts("Guest X86 frame + ARM64 host stack bridge A-B-A-B: passed");
    return 0;
}
