#include "portable_xbe_dispatch.h"
#include <cassert>
extern "C" const uint32_t X86CodeBase=0x1000,X86CodeEnd=0x3000,X86EntryPoint=0x1000;
extern "C" void sub_00002000(X86Context& ctx,uint8_t* base){ctx.eax=0x1234;base[0]=42;ctx.esp+=4;}
extern "C" void sub_00001000(X86Context& ctx,uint8_t* base){x86_icall(ctx,base,0x2000);}
extern "C" const X86FuncMapping X86FuncMappings[]={{0x1000,sub_00001000},{0,nullptr}};
extern "C" int hulk_entry_execute(X86Context*,uint8_t*,uint32_t);
int main(){
    X86Context ctx{};ctx.esp=0x1000;ctx.fs_base=1;ctx.thread_handle=1;
    uint8_t dummy=0;
    assert(hulk_entry_execute(&ctx,&dummy,0x1000)==0);
    assert(ctx.last_icall==0x2000&&ctx.eax==0x1234&&ctx.esp==0x1004&&dummy==42);
    assert(hulk_entry_execute(&ctx,&dummy,0x2001)==1);
}
