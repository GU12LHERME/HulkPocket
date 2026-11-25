#include "xbox_fiber_scheduler.h"
#include <stdexcept>
#include <cstdio>
#include <vector>

using hulkpocket::XboxFiberScheduler;
struct Trace {
    XboxFiberScheduler* scheduler;
    std::vector<int> steps;
};
void first(void* p) {
    auto& t = *static_cast<Trace*>(p);
    t.steps.push_back(1);
    t.scheduler->SwitchTo(2);
    t.steps.push_back(3);
    t.scheduler->SwitchTo(2);
    t.steps.push_back(5);
}
void second(void* p) {
    auto& t = *static_cast<Trace*>(p);
    t.steps.push_back(2);
    t.scheduler->SwitchTo(1);
    t.steps.push_back(4);
    t.scheduler->SwitchTo(1);
    t.steps.push_back(6);
}
int main() {
    XboxFiberScheduler scheduler;
    Trace state{&scheduler, {}};
    scheduler.Create(1, first, &state);
    scheduler.Create(2, second, &state);
    bool rejected = false;
    try { scheduler.Create(1, first, &state); }
    catch (const std::logic_error&) { rejected = true; }
    if (!rejected) return 1;
    scheduler.Run(1);       // A -> B -> A -> B -> A, A completes
    if (state.steps != std::vector<int>({1, 2, 3, 4, 5}) ||
        !scheduler.Finished(1) || scheduler.Finished(2)) return 2;
    scheduler.Run(2);       // B resumes exactly after its second SwitchTo
    if (state.steps != std::vector<int>({1, 2, 3, 4, 5, 6}) ||
        !scheduler.Finished(2)) return 3;
    rejected = false;
    try { scheduler.Run(1); }
    catch (const std::logic_error&) { rejected = true; }
    if (!rejected) return 4;
    std::puts("Native Xbox guest fiber dispatcher: A-B-A-B-A then B resume passed");
    return 0;
}
