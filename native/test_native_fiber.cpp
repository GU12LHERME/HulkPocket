#include "native_fiber.h"
#include <cstdio>
#include <stdexcept>
#include <string>

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
static void throwing_entry(void* ptr) {
    auto& state=*static_cast<State*>(ptr);
    state.step = 4;
    NativeFiber::Yield();
    state.step = 5;
    throw std::runtime_error("guest runtime stopped");
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
    State failure{};
    NativeFiber failing(throwing_entry, &failure);
    if (!failing.Resume() || failure.step != 4) return 6;
    bool propagated=false;
    try { failing.Resume(); }
    catch (const std::runtime_error& e) {
        propagated=std::string(e.what())=="guest runtime stopped";
    }
    if (!propagated || !failing.Finished() ||
        failure.step != 5 || failing.Resume()) return 7;
    std::puts("Native ARM64 fiber lifecycle and guard-page stack: passed");
    return 0;
}
