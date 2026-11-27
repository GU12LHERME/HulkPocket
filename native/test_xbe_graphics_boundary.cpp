#include "xbe_graphics_boundary.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <array>
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
extern "C" void sub_003441F0(X86Context&,uint8_t*);
extern "C" void sub_003442BC(X86Context&,uint8_t*);
extern "C" void sub_0034420D(X86Context&,uint8_t*);
extern "C" void sub_003442D9(X86Context&,uint8_t*);
extern "C" void sub_00346694(X86Context&,uint8_t*);
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
    // Exact USA symbol addresses; x87 stack TOP can wrap and cdecl
    // return must pop only the return address, not the arguments.
    {
        X86Context m{};m.top=7;m.eax=123;m.zf=1;m.st[0]=77;
        auto unary=[&](X86Func* f,double arg,double expected){
            m.esp=32;m.st[7]=arg;f(m,nullptr);
            assert(m.esp==36&&m.top==7&&m.eax==123&&m.zf==1&&m.st[0]==77);
            assert(std::abs(m.st[7]-expected)<1e-12);
        };
        unary(sub_003441F0,0,std::acos(0.));
        unary(sub_003442BC,.5,std::asin(.5));
        unary(sub_0034420D,1,0);
        unary(sub_003442D9,-.5,std::asin(-.5));
        std::array<uint8_t,128> mem{};
        double x=2,y=3;std::memcpy(mem.data()+36,&x,8);std::memcpy(mem.data()+44,&y,8);
        m.esp=32;m.st[7]=3;sub_00346694(m,mem.data());
        assert(m.st[7]==8&&m.esp==36&&m.top==7&&m.st[0]==77);
        m.esp=32;m.st[7]=2;sub_003441F0(m,nullptr);
        assert(std::isnan(m.st[7])&&m.esp==36); // do not clamp genuine domain errors
    }
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
    // Compact Vulkan logging samples hot boundaries but does not affect
    // the full-capture or legacy paths.
    setenv("HULK_RENDERER","vulkan",1);setenv("HULK_COMPACT_REPORT","1",1);
    unsetenv("HULK_FULL_DRAW_CAPTURE");
    unsigned trace_count=0;
    for(unsigned i=1;i<=1024;++i)assert(GraphicsTraceSample(trace_count)==(i<=16||i==1024));
    setenv("HULK_FULL_DRAW_CAPTURE","1",1);assert(GraphicsTraceSample(trace_count));
    unsetenv("HULK_FULL_DRAW_CAPTURE");setenv("HULK_RENDERER","gles",1);
    assert(GraphicsTraceSample(trace_count));
    unsetenv("HULK_RENDERER");unsetenv("HULK_COMPACT_REPORT");
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
    assert(DiagnosticSetterLimit(10800)==1382400);
    assert(DiagnosticSetterLimit(0xffffffffu)==1382400);
    unsetenv("HULK_WATCHDOG_SECONDS");
    assert(DiagnosticWatchdogSeconds()==15&&DiagnosticAudioSeconds()==120);
    assert(setenv("HULK_WATCHDOG_SECONDS","240",1)==0);
    assert(DiagnosticWatchdogSeconds()==240&&DiagnosticAudioSeconds()==240);
    assert(setenv("HULK_WATCHDOG_SECONDS","600",1)==0);
    assert(DiagnosticWatchdogSeconds()==600&&DiagnosticAudioSeconds()==600);
    assert(setenv("HULK_WATCHDOG_SECONDS","601",1)==0);
    assert(DiagnosticWatchdogSeconds()==15&&DiagnosticAudioSeconds()==120);
    unsetenv("HULK_WATCHDOG_SECONDS");
    assert(DiagnosticTracePoll(1)&&DiagnosticTracePoll(16)&&!DiagnosticTracePoll(17)&&DiagnosticTracePoll(1024));
    assert(setenv("HULK_FRAME_LIMIT","1800",1)==0);
    assert(DiagnosticFrameLimit()==1800);
    swap(600,true);swap(1799,true);swap(1800,false);
    assert(setenv("HULK_FRAME_LIMIT","5400",1)==0);
    assert(DiagnosticFrameLimit()==5400);
    swap(1800,true);swap(5399,true);swap(5400,false);
    assert(setenv("HULK_FRAME_LIMIT","10800",1)==0);
    assert(DiagnosticFrameLimit()==10800);
    swap(5400,true);swap(10799,true);swap(10800,false);
    assert(setenv("HULK_FRAME_LIMIT","10801",1)==0);
    assert(DiagnosticFrameLimit()==60);swap(60,false);
    assert(setenv("HULK_FRAME_LIMIT","60oops",1)==0);
    assert(DiagnosticFrameLimit()==60);
    unsetenv("HULK_FRAME_LIMIT");
}
