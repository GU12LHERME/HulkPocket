#include "xbe_startup_layout.h"
#include <cassert>
using namespace hulkpocket;
int main() {
    LoadedXbeImage image{};
    image.base=0x10000; image.image_size=0x500000;
    image.stack_commit=0x10000; image.heap_reserve=0x100000;
    image.heap_commit=0x1000;
    for (auto page : {4096u,16384u}) {
        auto l=PlanXbeStartupLayout(image,0x1000000,0x1200000,page);
        assert(l.stack_base==l.guard+page);
        assert(l.stack_top-l.stack_base==0x10000);
        assert(l.heap_base==l.stack_top);
        assert(l.heap_reserved==0x100000);
        assert(l.heap_committed==page);
    }
    auto rejects=[&](unsigned base,unsigned end,unsigned page) {
        try { PlanXbeStartupLayout(image,base,end,page); }
        catch(const std::invalid_argument&) { return true; }
        return false;
    };
    assert(rejects(0x10000,0x300000,4096));
    assert(rejects(0x1000000,0x1010000,4096));
    assert(rejects(0x1000000,0x1200000,0));
    image.heap_commit=0x200000;
    assert(rejects(0x1000000,0x1200000,4096));
}
