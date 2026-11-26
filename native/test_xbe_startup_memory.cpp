#include "xbe_startup_memory.h"
#include <cassert>
using namespace hulkpocket;
int main() {
    XboxGuestAddressSpace memory;
    LoadedXbeImage image{};
    image.base=0x10000; image.image_size=0x500000;
    image.stack_commit=0x10000; image.heap_reserve=0x100000;
    image.heap_commit=0x1000;
    const auto l=InitializeXbeStartupMemory(image,memory,0x1000000,0x1200000);
    assert(!memory.IsCommitted(l.guard,1));
    assert(memory.IsCommitted(l.stack_base,l.stack_top-l.stack_base));
    assert(memory.IsCommitted(l.heap_base,l.heap_committed));
    assert(!memory.IsCommitted(l.heap_base+l.heap_committed,1));
    assert(memory.Read32(l.stack_top-4)==0);
    assert(memory.Read32(l.heap_base)==0);
    memory.Write32(l.stack_top-4,0x12345678);
    assert(memory.Read32(l.stack_top-4)==0x12345678);
    bool rejected=false;
    try { InitializeXbeStartupMemory(image,memory,0x1000000,0x1200000); }
    catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
}
