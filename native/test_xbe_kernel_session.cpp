#include "xbe_kernel_session.h"
#include "hulk_kernel_objects.h"
#include <cassert>
#include <atomic>
#include <cstring>
using namespace hulkpocket;
namespace {
XboxGuestAddressSpace* memory;
std::atomic<unsigned> ran{0};
void call(X86Context& c, uint8_t* base,XbeKernelSession::Callback cb,
          uint32_t ordinal,std::initializer_list<uint32_t> args) {
    const auto old=c.esp;
    c.esp-=static_cast<uint32_t>(4+4*args.size());
    memory->Write32(c.esp,0);
    unsigned i=0;
    for(auto a:args) memory->Write32(c.esp+4+4*i++,a);
    cb(c,base,ordinal);
    assert(c.esp==old);
}
int entry(X86Context* c,uint8_t* base,uint32_t addr,XbeKernelSession::Callback cb) {
    if(addr==0x13000){
        assert(memory->Read32(c->esp+4)==0x12500);
        assert(memory->Read32(c->esp+8)==0x12345678);
        auto arg1=memory->Read32(c->esp+12),arg2=memory->Read32(c->esp+16);
        memory->Write32(0x12580,arg1);memory->Write32(0x12584,arg2);
        memory->Write32(0x12588,memory->Read32(0x12588)+1);
        call(*c,base,cb,145,{0x12540,0,0});
        c->esp+=20;return 0;
    }
    if(addr==0x2000) {
        assert(memory->Read32(c->esp+4)==0x3000);
        assert(memory->Read32(c->esp+8)==0x1234);
        const auto thread=memory->Read32(c->fs_base+0x28);
        const auto tls=memory->Read32(thread+0x28);
        assert(memory->Read32(tls)==0xdeadbeef);
        assert(memory->Read32(tls+4)==0);
        assert(memory->Read32(c->fs_base+4)-tls==24);
        assert(memory->Read32(c->fs_base+4)-memory->Read32(c->fs_base+8)>0x80000);
        call(*c,base,cb,104,{});
        assert(c->eax==thread);
        call(*c,base,cb,277,{0x10580});
        assert(memory->Read32(0x10598)==thread);
        call(*c,base,cb,294,{0x10580});
        ++ran;
        call(*c,base,cb,258,{42});
        assert(false);
    }
    // Unavailable physical Xbox device drivers return explicit failure.
    // Never invent a successful device object or leave stale output.
    memory->Write32(0x12a00,0xdeadbeef);
    call(*c,base,cb,65,{0,64,0,0x22,0,0x12a00});
    assert(c->eax==0xc0000001u&&memory->Read32(0x12a00)==0);
    call(*c,base,cb,65,{0,64,0,0x22,0,0});
    assert(c->eax==0xc000000du);
    memory->Write32(0x12a04,0xdeadbeef);
    memory->Write32(0x12a08,0xdeadbeef);
    call(*c,base,cb,84,{1,0,0,0,0x12a04,4,0x12a08,0});
    assert(c->eax==0xc0000001u&&memory->Read32(0x12a04)==0&&memory->Read32(0x12a08)==0);
    call(*c,base,cb,84,{1,0,0,0,0,4,0x12a08,0});
    assert(c->eax==0xc000000du);
    call(*c,base,cb,85,{3,0,0,0,0});assert(c->eax==0xc0000001u);
    memory->Write32(0x12a0c,0xaabbccdd);
    call(*c,base,cb,44,{5,0x12a0c});
    assert(c->eax==0x35&&memory->Read32(0x12a0c)==0xaabbcc05);
    // Main-thread settings and fastcall object references.
    const auto main_body=memory->Read32(c->fs_base+0x28);
    call(*c,base,cb,246,{0xfffffffeu,0,0x10f00});
    assert(c->eax==0&&memory->Read32(0x10f00)==main_body);
    call(*c,base,cb,143,{main_body,3});assert(c->eax==0);
    call(*c,base,cb,124,{main_body});assert(c->eax==3);
    call(*c,base,cb,143,{main_body,uint32_t(-2)});assert(c->eax==3);
    call(*c,base,cb,124,{main_body});assert(c->eax==uint32_t(-2));
    call(*c,base,cb,144,{main_body,1});assert(c->eax==0);
    call(*c,base,cb,144,{main_body,0});assert(c->eax==1);
    c->ecx=main_body;call(*c,base,cb,251,{});
    c->ecx=main_body;call(*c,base,cb,250,{});
    c->ecx=main_body;call(*c,base,cb,250,{});
    call(*c,base,cb,238,{});
    memory->Write32(0x10e00,0x11000);
    memory->Write32(0x10e04,1);
    call(*c,base,cb,47,{0x10e00,1});
    call(*c,base,cb,47,{0x10e00,0});
    call(*c,base,cb,35,{});assert(c->eax==16);
    call(*c,base,cb,37,{64});assert(c->eax==0);
    call(*c,base,cb,35,{});assert(c->eax==64);
    call(*c,base,cb,24,{0x104,0x10e20,0x10e24,4,0x10e28});
    assert(c->eax==0 && memory->Read32(0x10e20)==4 && memory->Read32(0x10e24)==1 && memory->Read32(0x10e28)==4);
    call(*c,base,cb,24,{7,0,0,0,0x10e28});assert(c->eax==0xc0000023u);
    call(*c,base,cb,301,{0xc0000034u});assert(c->eax==2);
    call(*c,base,cb,127,{});assert(c->eax==3375000&&c->edx==0);
    call(*c,base,cb,125,{});uint64_t before=(uint64_t(c->edx)<<32)|c->eax;
    call(*c,base,cb,151,{100});
    call(*c,base,cb,125,{});assert(((uint64_t(c->edx)<<32)|c->eax)>=before+1000);
    XboxTimeFields fields{2000,2,29,12,34,56,789,0};
    memory->Write(0x12400,&fields,sizeof(fields));
    call(*c,base,cb,304,{0x12400,0x12420});assert(c->eax==1);
    call(*c,base,cb,305,{0x12420,0x12440});
    XboxTimeFields result{};memory->Read(0x12440,&result,sizeof(result));
    assert(result.year==2000&&result.month==2&&result.day==29&&result.hour==12&&result.minute==34&&result.second==56&&result.milliseconds==789&&result.weekday==2);
    fields.year=1900;memory->Write(0x12400,&fields,sizeof(fields));
    call(*c,base,cb,304,{0x12400,0x12420});assert(c->eax==0);
    uint64_t zero=0;memory->Write(0x12420,&zero,8);
    call(*c,base,cb,305,{0x12420,0x12440});memory->Read(0x12440,&result,sizeof(result));
    assert(result.year==1601&&result.month==1&&result.day==1&&result.weekday==1);
    // Initialize a real KDPC, execute a nested guest callback, then
    // schedule a timer whose callback signals a direct event.
    call(*c,base,cb,129,{});assert(c->eax==0);
    call(*c,base,cb,103,{});assert(c->eax==2);
    c->ecx=0;call(*c,base,cb,161,{});
    call(*c,base,cb,103,{});assert(c->eax==0);
    c->ecx=3;call(*c,base,cb,160,{});assert(c->eax==0);
    c->ecx=0;call(*c,base,cb,161,{});
    call(*c,base,cb,358,{});assert(c->eax==0);
    call(*c,base,cb,107,{0x12500,0x13000,0x12345678});
    assert(memory->Read32(0x12500)==19&&memory->Read32(0x1250c)==0x13000);
    call(*c,base,cb,108,{0x12540,1,0});
    call(*c,base,cb,119,{0x12500,0x1111,0x2222});
    assert(c->eax==1&&memory->Read32(0x12580)==0x1111&&memory->Read32(0x12584)==0x2222);
    assert(memory->Read32(0x12588)==1&&memory->Read32(0x12504)==0);
    call(*c,base,cb,159,{0x12540,0,0,0,0});assert(c->eax==0&&memory->Read32(0x12544)==0);
    call(*c,base,cb,113,{0x12600,1});
    assert(memory->Read32(0x12600)==0x000a0009&&memory->Read32(0x12608)==0x12608);
    // Relative one-shot timer (2 ms). Pointer wait pumps the DPC.
    uint64_t due=uint64_t(int64_t(-20000));
    call(*c,base,cb,149,{0x12600,uint32_t(due),uint32_t(due>>32),0x12500});
    call(*c,base,cb,159,{0x12540,0,0,0,0});assert(c->eax==0&&memory->Read32(0x12588)==2);
    call(*c,base,cb,159,{0x12600,0,0,0,0});assert(c->eax==0&&memory->Read32(0x12604)==0);
    due=uint64_t(int64_t(-100000000));
    call(*c,base,cb,149,{0x12600,uint32_t(due),uint32_t(due>>32),0});
    call(*c,base,cb,97,{0x12600});assert(c->eax==1);
    call(*c,base,cb,97,{0x12600});assert(c->eax==0);
    // Timer handles, aliases, byte-sized previous state and wait consumption.
    call(*c,base,cb,194,{0x12800,0,1});assert(c->eax==0);
    auto timer=memory->Read32(0x12800);
    call(*c,base,cb,197,{timer,0x12804,1});assert(c->eax==0);
    timer=memory->Read32(0x12804);
    due=uint64_t(int64_t(-20000));memory->Write(0x12810,&due,8);
    memory->Write32(0x12818,0xaabbccdd);
    call(*c,base,cb,229,{timer,0x12810,0,0,0,0,0,0x12818});
    assert(c->eax==0&&memory->Read32(0x12818)==0xaabbcc00);
    call(*c,base,cb,233,{timer,0,0});assert(c->eax==0);
    uint64_t instant=0;memory->Write(0x12820,&instant,8);
    call(*c,base,cb,233,{timer,0,0x12820});assert(c->eax==0x102);
    call(*c,base,cb,185,{timer,0x12818});assert(c->eax==0);
    call(*c,base,cb,187,{timer});assert(c->eax==0);
    call(*c,base,cb,189,{0x12824,0,1,0});assert(c->eax==0);
    auto signal_event=memory->Read32(0x12824);
    call(*c,base,cb,230,{signal_event,signal_event,0,0,0});assert(c->eax==0);
    call(*c,base,cb,233,{signal_event,0,0x12820});assert(c->eax==0x102);
    call(*c,base,cb,187,{signal_event});assert(c->eax==0);
    // Requests for non-recompiled executable images return the same
    // error as the upstream XAPI replacement instead of rebooting.
    const char launch[]="C:\\xboxdash.xbe";
    memory->Write(0x12700,launch,sizeof(launch));
    call(*c,base,cb,kXapiLaunchImage,{0x12700,0});assert(c->eax==2);
    call(*c,base,cb,kXapiLaunchImage,{0,0});assert(c->eax==2);
    {
        auto old=c->esp;c->esp-=4;memory->Write32(c->esp,0x123456);
        c->last_icall=0x003cf9b8;bool stopped=false;
        try{cb(*c,base,kMissingGuestEntry);}catch(const std::runtime_error&){stopped=true;}
        assert(stopped&&c->esp==old-4&&c->last_icall==0x003cf9b8);
        c->esp=old;
    }
    // Diagnostic stop must not pretend to allocate or pop the guest stack.
    {
        auto old=c->esp;
        c->esp-=32;
        memory->Write32(c->esp,0x266e1c);
        memory->Write32(c->esp+4,0x100);
        memory->Write32(c->esp+20,0x123456);
        memory->Write32(c->esp+24,2);
        memory->Write32(c->esp+28,0x100);
        bool stopped=false;
        try{cb(*c,base,kHulkAllocatorStop);}catch(const std::runtime_error&){stopped=true;}
        assert(stopped&&c->esp==old-32);
        c->esp=old;
    }
    // SHA-1 of "abc" and RFC 2202 HMAC-SHA1 vector, through the guest ABI.
    const uint8_t sha[]={0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d};
    const uint8_t mac[]={0xb6,0x17,0x31,0x86,0x55,0x05,0x72,0x64,0xe2,0x8b,0xc0,0xb6,0xfb,0x37,0x8c,0x8e,0xf1,0x46,0xbe,0x00};
    memory->Write(0x12000,"abc",3);
    call(*c,base,cb,335,{0x12100});
    call(*c,base,cb,336,{0x12100,0x12000,1});
    call(*c,base,cb,336,{0x12100,0x12001,2});
    call(*c,base,cb,337,{0x12100,0x12200});
    assert(std::memcmp(base+0x12200,sha,20)==0);
    uint8_t key[20];std::memset(key,0x0b,sizeof(key));
    memory->Write(0x12020,key,sizeof(key));memory->Write(0x12040,"Hi There",8);
    call(*c,base,cb,340,{0x12020,20,0x12040,3,0x12043,5,0x12220});
    assert(std::memcmp(base+0x12220,mac,20)==0);
    call(*c,base,cb,285,{0x12300,9,0xaabbccdd});
    assert(memory->Read32(0x12300)==0xaabbccdd && memory->Read32(0x12304)==0xaabbccdd && memory->Read32(0x12308)==0);
    call(*c,base,cb,269,{0x12300,12,0xaabbccdd});assert(c->eax==8);
    call(*c,base,cb,268,{0x12000,0x12000,3});assert(c->eax==3);
    call(*c,base,cb,268,{0x12000,0x12001,2});assert(c->eax==0);
    const char name[]="D:\\data.bin";
    memory->Write(0x10900,name,sizeof(name));
    call(*c,base,cb,289,{0x10940,0x10900});
    memory->Write32(0x10950,0);memory->Write32(0x10954,0x10940);
    memory->Write32(0x10958,0);
    call(*c,base,cb,202,{0x10960,0x80000000,0x10950,0x10968,0,0x40});
    assert(c->eax==0);
    auto file=memory->Read32(0x10960);
    call(*c,base,cb,219,{file,0,0,0,0x10968,0x10980,4,0});
    assert(c->eax==0 && memory->Read32(0x10980)==0x6b6c7548);
    call(*c,base,cb,211,{file,0x10968,0x10990,24,5});
    assert(c->eax==0 && memory->Read32(0x10998)==4);
    call(*c,base,cb,219,{file,0,0,0,0x10968,0x10980,4,0});
    assert(c->eax==0xc0000011u);
    call(*c,base,cb,187,{file});
    assert(c->eax==0);
    const char escape[]="D:\\..\\outside";
    memory->Write(0x10900,escape,sizeof(escape));
    call(*c,base,cb,289,{0x10940,0x10900});
    call(*c,base,cb,202,{0x10960,0x80000000,0x10950,0x10968,0,0x40});
    assert(c->eax==0xc0000034u);
    memory->Write32(0x12800,35);
    call(*c,base,cb,181,{0x12800});assert(c->eax==0xc000000du);
    memory->Write32(0x12800,36);
    call(*c,base,cb,181,{0x12800});assert(c->eax==0&&memory->Read32(0x12804)==16384);
    auto available=memory->Read32(0x12808);
    call(*c,base,cb,166,{0x1000,0x100000,0x1fffff,0x10000,4});
    auto physical=c->eax;
    assert(physical>=0x80100000 && physical<0x80200000 && physical%0x10000==0);
    call(*c,base,cb,181,{0x12800});assert(c->eax==0&&memory->Read32(0x12808)==available-memory->PageSize()/4096);
    memory->Write32(physical,0xaabbccdd);
    call(*c,base,cb,173,{physical});
    assert(c->eax==physical-0x80000000);
    call(*c,base,cb,178,{physical,0x1000,1});
    call(*c,base,cb,176,{physical-0x80000000,0});
    call(*c,base,cb,176,{physical-0x80000000,1});
    call(*c,base,cb,175,{physical,0x1000,0});
    call(*c,base,cb,175,{physical,0x1000,1});
    call(*c,base,cb,179,{physical});assert(c->eax==4);
    call(*c,base,cb,174,{physical});assert(c->eax==1);
    call(*c,base,cb,171,{physical});
    assert(!memory->IsCommitted(physical,1));
    call(*c,base,cb,174,{physical});assert(c->eax==0);
    call(*c,base,cb,179,{physical});assert(c->eax==0);
    call(*c,base,cb,167,{5000,4});auto sys=c->eax;
    assert(sys&&sys%4096==0&&memory->Read32(sys)==0);
    call(*c,base,cb,180,{sys});assert(c->eax==8192);
    memory->Write32(sys,0xdeadbeef);
    call(*c,base,cb,172,{sys,5000});assert(c->eax==2);
    call(*c,base,cb,167,{5000,4});assert(c->eax==sys&&memory->Read32(sys)==0);
    call(*c,base,cb,172,{sys,5000});assert(c->eax==2);
    call(*c,base,cb,166,{0x1000,0x100000,0x1fffff,0x10000,4});
    assert(c->eax==physical && memory->Read32(physical)==0);
    call(*c,base,cb,171,{physical});
    memory->Write32(0x10800,0);memory->Write32(0x10804,0x2000);
    call(*c,base,cb,184,{0x10800,0,0x10804,0x2000,4});
    assert(c->eax==0);
    auto va=memory->Read32(0x10800);
    assert(va==0x10000000 && !memory->IsCommitted(va,1));
    call(*c,base,cb,217,{va,0x10820});
    assert(c->eax==0 && memory->Read32(0x10830)==0x2000);
    memory->Write32(0x10804,0x1000);
    call(*c,base,cb,184,{0x10800,0,0x10804,0x1000,4});
    assert(c->eax==0 && memory->IsCommitted(va,4));
    call(*c,base,cb,181,{0x12800});
    assert(c->eax==0&&memory->Read32(0x1280c)>=4096&&memory->Read32(0x12810)==0x10000);
    memory->Write32(va,0x12345678);
    call(*c,base,cb,180,{va});
    assert(c->eax==0x10000);
    call(*c,base,cb,199,{0x10800,0x10804,0x4000});
    assert(c->eax==0 && !memory->IsCommitted(va,1));
    call(*c,base,cb,184,{0x10800,0,0x10804,0x1000,4});
    assert(c->eax==0 && memory->Read32(va)==0);
    memory->Write32(0x10804,0);
    call(*c,base,cb,199,{0x10800,0x10804,0x8000});
    assert(c->eax==0 && !memory->IsCommitted(va,1));
    call(*c,base,cb,217,{va,0x10820});
    assert(c->eax==0 && memory->Read32(0x10830)==0x10000);
    // Top-down allocation, NOZERO modifier, inaccessible reserve,
    // and zero-bit constraint on the returned address.
    memory->Write32(0x10800,0);memory->Write32(0x10804,0x1000);
    call(*c,base,cb,184,{0x10800,0,0x10804,0x102000,1});
    assert(c->eax==0&&memory->Read32(0x10800)==0x2fff0000&&!memory->IsCommitted(0x2fff0000,1));
    memory->Write32(0x10804,0x1000);
    call(*c,base,cb,184,{0x10800,0,0x10804,0x801000,4});
    assert(c->eax==0&&memory->Read32(0x2fff0000)==0);
    memory->Write32(0x10804,0);
    call(*c,base,cb,199,{0x10800,0x10804,0x8000});assert(c->eax==0);
    memory->Write32(0x10800,0);memory->Write32(0x10804,0x1000);
    call(*c,base,cb,184,{0x10800,3,0x10804,0x103000,4});
    assert(c->eax==0&&memory->Read32(0x10800)==0x1fff0000);
    memory->Write32(0x10804,0);
    call(*c,base,cb,199,{0x10800,0x10804,0x8000});assert(c->eax==0);
    memory->Write32(0x10800,0);memory->Write32(0x10804,0x1000);
    call(*c,base,cb,184,{0x10800,4,0x10804,0x3000,4});assert(c->eax==0xc0000017u);
    memory->Write(0x10300,"Hulk",5);
    memory->Write(0x10310,"hULK",5);
    call(*c,base,cb,289,{0x10400,0x10300});
    call(*c,base,cb,289,{0x10408,0x10310});
    call(*c,base,cb,279,{0x10400,0x10408,1});
    assert(c->eax==1);
    call(*c,base,cb,279,{0x10400,0x10408,0});
    assert(c->eax==0);
    call(*c,base,cb,189,{0x10110,0,1,0});
    assert(c->eax==0);
    auto ev=memory->Read32(0x10110);
    call(*c,base,cb,246,{ev,0x10c00,0x10b00});
    assert(c->eax==0 && memory->Read32(0x10b00)!=0);
    call(*c,base,cb,246,{ev,0x10d00,0x10b00});
    assert(c->eax==0xc0000024u);
    memory->Write32(0x10118,0);memory->Write32(0x1011c,0);
    call(*c,base,cb,233,{ev,0,0x10118});
    assert(c->eax==0x102);
    call(*c,base,cb,225,{ev,0x10114});
    assert(c->eax==0 && memory->Read32(0x10114)==0);
    call(*c,base,cb,233,{ev,0,0x10118});
    assert(c->eax==0);
    call(*c,base,cb,233,{ev,0,0x10118});
    assert(c->eax==0x102);
    call(*c,base,cb,187,{ev});
    assert(c->eax==0);
    call(*c,base,cb,233,{ev,0,0x10118});
    assert(c->eax==0xc0000008u);
    call(*c,base,cb,193,{0x10a00,0,0,2});
    assert(c->eax==0);
    auto sem=memory->Read32(0x10a00);
    call(*c,base,cb,222,{sem,2,0x10a04});
    assert(c->eax==0 && memory->Read32(0x10a04)==0);
    call(*c,base,cb,222,{sem,1,0});
    assert(c->eax==0xc0000047u);
    call(*c,base,cb,189,{0x10a08,0,1,0});
    auto ev2=memory->Read32(0x10a08);
    memory->Write32(0x10a10,ev2);
    memory->Write32(0x10a14,sem);
    call(*c,base,cb,235,{2,0x10a10,0,0,0,0x10118});
    assert(c->eax==0x102);
    call(*c,base,cb,235,{2,0x10a10,1,0,0,0x10118});
    assert(c->eax==1);
    call(*c,base,cb,225,{ev2,0});
    call(*c,base,cb,235,{2,0x10a10,0,0,0,0x10118});
    assert(c->eax==0);
    call(*c,base,cb,235,{2,0x10a10,1,0,0,0x10118});
    assert(c->eax==0x102);
    call(*c,base,cb,187,{sem});
    call(*c,base,cb,187,{ev2});
    call(*c,base,cb,291,{0x10500});
    call(*c,base,cb,277,{0x10500});
    call(*c,base,cb,277,{0x10500});
    assert(memory->Read32(0x10514)==2);
    call(*c,base,cb,294,{0x10500});
    call(*c,base,cb,294,{0x10500});
    assert(memory->Read32(0x10510)==0xffffffff);
    // Zero-initialized CRT section, then a section initialized in guest code.
    call(*c,base,cb,277,{0x10540});
    assert(memory->Read32(0x10558)==0x10700);
    call(*c,base,cb,294,{0x10540});
    memory->Write32(0x10580,0x00040001);
    memory->Write32(0x10588,0x10588);
    memory->Write32(0x1058c,0x10588);
    memory->Write32(0x10590,0xffffffff);
    call(*c,base,cb,277,{0x10580});
    call(*c,base,cb,255,{0x10100,0,0x100000,24,0x10104,0x3000,0x1234,1,0,0x2000});
    assert(c->eax==0 && ran==0);
    const auto h=memory->Read32(0x10100);
    assert(h!=memory->Read32(0x10104));
    call(*c,base,cb,246,{h,0,0x10f04});assert(c->eax==0);
    auto child_body=memory->Read32(0x10f04);
    call(*c,base,cb,143,{child_body,5});assert(c->eax==0);
    call(*c,base,cb,124,{child_body});assert(c->eax==5);
    c->ecx=child_body;call(*c,base,cb,250,{});
    call(*c,base,cb,231,{h,0x10108});
    assert(memory->Read32(0x10108)==1);
    call(*c,base,cb,224,{h,0x10108});
    assert(memory->Read32(0x10108)==2 && ran==0);
    call(*c,base,cb,224,{h,0x10108});
    assert(memory->Read32(0x10108)==1);
    call(*c,base,cb,224,{0xdead,0});
    assert(c->eax==0xc0000008u);
    call(*c,base,cb,294,{0x10580});
    call(*c,base,cb,233,{h,0,0});
    assert(c->eax==0 && ran==1);
    return 0;
}
}
int main() {
    XboxGuestAddressSpace mem;
    InitializeXboxKernelPhysicalRegion(mem);
    {
        XbePhysicalMemory physical(mem);
        assert(physical.Persist(0x80010000,4096,true));
        assert(physical.IsPersistent(0x80010001));
        assert(physical.Persist(0x80010000,4096,false));
        assert(!physical.IsPersistent(0x80010000));
        assert(!physical.Persist(0x84000000,4096,true));
        auto a=physical.Allocate(4096,0x100000,0x3ffffff,4096);
        assert(a&&physical.Persist(a,4096,true)&&physical.IsPersistent(a));
        assert(physical.Free(a)&&!physical.IsPersistent(a));
    }
    memory=&mem;
    mem.Commit(0x10000,0x10000);
    {
        HulkKernelDataRegistry clock_registry;
        HulkKernelClock clock(mem,clock_registry,0x12900,10000);
        HulkKernelClockPump pump(mem,&clock);
        auto* tick=reinterpret_cast<uint32_t*>(mem.Base()+0x12900);
        auto before=__atomic_load_n(tick,__ATOMIC_RELAXED);
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        // No kernel calls in this loop: the guest tick must still advance.
        while(__atomic_load_n(tick,__ATOMIC_RELAXED)==before&&std::chrono::steady_clock::now()<deadline)
            std::this_thread::yield();
        assert(__atomic_load_n(tick,__ATOMIC_RELAXED)>before);
    }
    mem.Write32(0x10200,0xdeadbeef);
    XbeTlsTemplate tls{0x10200,0x10204,0,0,4,0};
    X86Context cpu{};
    cpu.esp=0x1fff0; cpu.thread_handle=0x10; cpu.fs_base=0x10600;
    mem.Write32(0x10628,0x10700);
    char dir[]="/tmp/hulk-disc-XXXXXX";
    assert(mkdtemp(dir));
    std::string path=std::string(dir)+"/DATA.BIN";
    int fd=open(path.c_str(),O_CREAT|O_WRONLY|O_EXCL,0600);
    assert(fd>=0 && write(fd,"Hulk",4)==4);
    close(fd);
    HulkKernelDataRegistry registry;
    RegisterDispatcherObjectTypes(mem,registry,0x10c00);
    XbeKernelSession session(mem,0x09000000,0x01000000,tls,nullptr,dir,&registry);
    assert(session.Run(entry,cpu,0x1000)==0);
    assert(ran==1);
    assert(mem.Read32(0x0A000404)==1);
    assert(unlink(path.c_str())==0 && rmdir(dir)==0);
}
