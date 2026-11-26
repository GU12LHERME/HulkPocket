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
extern "C" void sub_00381DF0(X86Context&,uint8_t*);
using namespace hulkpocket;
static uint32_t swap_count=0;
static bool complete_swap=true;
static bool entered=false,complete_create=false;
static void callback(X86Context& c,uint8_t*,uint32_t id){
    assert(c.esp==0x1000);
    if(id==kGraphicsSwap){c.eax=swap_count;if(complete_swap)c.esp+=8;return;}
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
    auto swap=[&](uint32_t n,bool accept){
        c.esp=0x1000;swap_count=n;bool rejected=false;
        try{sub_00381DF0(c,nullptr);}catch(const std::runtime_error&){rejected=true;}
        assert(rejected!=accept);
        if(accept)assert(c.eax==n&&c.esp==0x1008);
    };
    unsetenv("HULK_FRAME_LIMIT");
    assert(DiagnosticFrameLimit()==60);
    swap(59,true);swap(60,false);swap(0,false);
    assert(setenv("HULK_FRAME_LIMIT","600",1)==0);
    assert(DiagnosticFrameLimit()==600);
    swap(60,true);swap(599,true);swap(600,false);
    complete_swap=false;swap(60,false);complete_swap=true;
    assert(DiagnosticSetterLimit(60)==16384);
    assert(DiagnosticSetterLimit(600)==76800);
    assert(DiagnosticSetterLimit(1800)==230400);
    assert(DiagnosticSetterLimit(5400)==691200);
    assert(DiagnosticSetterLimit(0xffffffffu)==691200);
    unsetenv("HULK_WATCHDOG_SECONDS");
    assert(DiagnosticWatchdogSeconds()==15&&DiagnosticAudioSeconds()==120);
    assert(setenv("HULK_WATCHDOG_SECONDS","240",1)==0);
    assert(DiagnosticWatchdogSeconds()==240&&DiagnosticAudioSeconds()==240);
    assert(setenv("HULK_WATCHDOG_SECONDS","241",1)==0);
    assert(DiagnosticWatchdogSeconds()==15&&DiagnosticAudioSeconds()==120);
    unsetenv("HULK_WATCHDOG_SECONDS");
    assert(DiagnosticTracePoll(1)&&DiagnosticTracePoll(16)&&!DiagnosticTracePoll(17)&&DiagnosticTracePoll(1024));
    assert(setenv("HULK_FRAME_LIMIT","1800",1)==0);
    assert(DiagnosticFrameLimit()==1800);
    swap(600,true);swap(1799,true);swap(1800,false);
    assert(setenv("HULK_FRAME_LIMIT","5400",1)==0);
    assert(DiagnosticFrameLimit()==5400);
    swap(1800,true);swap(5399,true);swap(5400,false);
    assert(setenv("HULK_FRAME_LIMIT","5401",1)==0);
    assert(DiagnosticFrameLimit()==60);swap(60,false);
    assert(setenv("HULK_FRAME_LIMIT","60oops",1)==0);
    assert(DiagnosticFrameLimit()==60);
    unsetenv("HULK_FRAME_LIMIT");
}
