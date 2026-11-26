#include "xbe_graphics_boundary.h"
#include <cassert>
#include <stdexcept>
extern "C" void __imp__sub_0037D900(X86Context&,uint8_t*){assert(false&&"original hardware routine executed");}
extern "C" void sub_0037D900(X86Context&,uint8_t*) __attribute__((weak,alias("__imp__sub_0037D900")));
extern "C" void __imp__sub_00209C04(X86Context&,uint8_t*){assert(false&&"original launch executed");}
extern "C" void sub_00209C04(X86Context&,uint8_t*) __attribute__((weak,alias("__imp__sub_00209C04")));
extern "C" void __imp__sub_003EAC33(X86Context&,uint8_t*){assert(false&&"original USB init executed");}
extern "C" void sub_003EAC33(X86Context&,uint8_t*) __attribute__((weak,alias("__imp__sub_003EAC33")));
extern "C" void __imp__sub_003EB6E5(X86Context&,uint8_t*){assert(false&&"original XInputOpen executed");}
extern "C" void sub_003EB6E5(X86Context&,uint8_t*) __attribute__((weak,alias("__imp__sub_003EB6E5")));
using namespace hulkpocket;
static bool entered=false,complete_create=false;
static void callback(X86Context& c,uint8_t*,uint32_t id){
    assert(c.esp==0x1000);
    if(id==kXapiLaunchImage){c.eax=2;c.esp+=12;return;}
    if(id==kXapiInitDevices){assert(c.ecx==0x3ea774&&c.edx==0x3ea820);c.eax=0;c.esp+=12;return;}
    if(id==kXapiInputOpen){c.eax=0;c.esp+=20;return;}
    assert(id==kGraphicsCreate&&c.ecx==0x39dfb0&&c.edx==0x39dfa8&&c.eax==0x3882e0);entered=true;
    if(complete_create){c.eax=0;c.esp+=28;}
}
int main(){
    ScopedGraphicsCallback scoped(callback);X86Context c{};c.esp=0x1000;
    bool stopped=false;
    try{sub_0037D900(c,nullptr);}catch(const std::runtime_error&){stopped=true;}
    assert(entered&&stopped&&c.esp==0x1000);
    complete_create=true;
    sub_0037D900(c,nullptr);assert(c.eax==0&&c.esp==0x101c);
    c.esp=0x1000;
    sub_00209C04(c,nullptr);assert(c.eax==2&&c.esp==0x100c);
    c.esp=0x1000;sub_003EAC33(c,nullptr);assert(c.eax==0&&c.esp==0x100c);
    c.esp=0x1000;sub_003EB6E5(c,nullptr);assert(c.eax==0&&c.esp==0x1014);
}
