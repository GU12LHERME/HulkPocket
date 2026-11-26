#include "xbe_graphics_boundary.h"
#include <cassert>
#include <stdexcept>
extern "C" void __imp__sub_0037D900(X86Context&,uint8_t*){assert(false&&"original hardware routine executed");}
extern "C" void sub_0037D900(X86Context&,uint8_t*) __attribute__((weak,alias("__imp__sub_0037D900")));
using namespace hulkpocket;
static bool entered=false;
static void callback(X86Context& c,uint8_t*,uint32_t id){assert(id==kGraphicsCreate&&c.esp==0x1000);entered=true;}
int main(){
    ScopedGraphicsCallback scoped(callback);X86Context c{};c.esp=0x1000;
    bool stopped=false;
    try{sub_0037D900(c,nullptr);}catch(const std::runtime_error&){stopped=true;}
    assert(entered&&stopped&&c.esp==0x1000);
}
