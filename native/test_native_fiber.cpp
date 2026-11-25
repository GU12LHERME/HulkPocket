#include "native_fiber.h"
#include <cstdio>
#include <stdexcept>

using hulkpocket::NativeFiber;

struct State { int step = 0; };
static void entry(void* ptr) {
    auto& state = *static_cast<State*>(ptr);
    state.step = 1;
    NativeFiber::Yield();
    state.step = 2;
    NativeFiber::Yield();
    state.step = 3;
}
int main() {
    State state;
    try {
        NativeFiber::Yield();
        return 1;
    } catch (const std::logic_error&) {}
    NativeFiber fiber(entry, &state);
    if (!fiber.Resume() || state.step != 1) return 2;
    if (!fiber.Resume() || state.step != 2) return 3;
    if (fiber.Resume() || state.step != 3 || !fiber.Finished()) return 4;
    if (fiber.Resume()) return 5;
    std::puts("Native ARM64 fiber lifecycle and guard-page stack: passed");
    return 0;
}
