#pragma once
#include <cstdint>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <ucontext.h>
#include <stdexcept>
namespace hulkpocket {
// Fault handler only uses fixed storage and async-signal-safe write/_exit.
inline uintptr_t fault_guest_base=0, fault_module_base=0;
inline thread_local volatile sig_atomic_t fault_ordinal=0;
inline thread_local volatile sig_atomic_t fault_thread=0;
inline thread_local volatile sig_atomic_t fault_return=0;
inline thread_local volatile sig_atomic_t fault_stack=0;
inline void FaultHex(char*& p,const char* label,uintptr_t value) noexcept {
    while(*label)*p++=*label++;
    for(int shift=int(sizeof(uintptr_t)*8)-4;shift>=0;shift-=4)
        *p++="0123456789ABCDEF"[(value>>shift)&15];
}
inline void GuestFaultHandler(int sig,siginfo_t* info,void* context) noexcept {
    char line[512];char* p=line;
    const auto address=sig==SIGALRM?uintptr_t(0):reinterpret_cast<uintptr_t>(info->si_addr);
    uintptr_t pc=0;
#if defined(__aarch64__)
    pc=static_cast<ucontext_t*>(context)->uc_mcontext.pc;
#elif defined(__x86_64__) && defined(REG_RIP)
    pc=static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_RIP];
#else
    (void)context;
#endif
    FaultHex(p,sig==SIGALRM?"Guest watchdog timeout signal=0x":"Guest fault signal=0x",static_cast<unsigned>(sig));
    FaultHex(p," host_address=0x",address);
    if(address>=fault_guest_base && uint64_t(address-fault_guest_base)<(1ULL<<32))
        FaultHex(p," guest_address=0x",address-fault_guest_base);
    else {
        const char* msg=" outside_guest_memory";
        while(*msg)*p++=*msg++;
    }
    FaultHex(p," pc=0x",pc);
    if(fault_module_base && pc>=fault_module_base)
        FaultHex(p," pc_minus_module_base=0x",pc-fault_module_base);
    FaultHex(p," guest_base=0x",fault_guest_base);
    FaultHex(p," thread=0x",static_cast<uint32_t>(fault_thread));
    FaultHex(p," last_kernel=0x",static_cast<uint32_t>(fault_ordinal));
    FaultHex(p," last_return=0x",static_cast<uint32_t>(fault_return));
    FaultHex(p," last_esp=0x",static_cast<uint32_t>(fault_stack));
    *p++='\n';
    (void)write(STDERR_FILENO,line,static_cast<size_t>(p-line));
#if defined(__aarch64__)
    // Raw ARM64 registers at the fault, not the older saved x86 context.
    // One bounded line per eight registers for easy local log inspection.
    const auto& regs=static_cast<ucontext_t*>(context)->uc_mcontext.regs;
    for(unsigned i=0;i<31;i+=8) {
        p=line;
        for(unsigned j=i;j<31 && j<i+8;++j) {
            char name[]=" x00=0x";
            name[2]=char('0'+j/10);name[3]=char('0'+j%10);
            FaultHex(p,name,regs[j]);
        }
        *p++='\n';(void)write(STDERR_FILENO,line,static_cast<size_t>(p-line));
    }
#endif
    _exit(128+sig);
}
inline void InstallGuestFaultHandler(uint8_t* base,uintptr_t module_base=0) {
    fault_guest_base=reinterpret_cast<uintptr_t>(base);
    fault_module_base=module_base;
    struct sigaction action{};
    action.sa_sigaction=GuestFaultHandler;
    action.sa_flags=SA_SIGINFO|SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    if(sigaction(SIGSEGV,&action,nullptr)||sigaction(SIGBUS,&action,nullptr)||
       sigaction(SIGILL,&action,nullptr)||sigaction(SIGALRM,&action,nullptr))
        throw std::runtime_error("Cannot install guest fault diagnostics");
}
// Prefer a still-running guest worker over the main thread waiting in
// pthread_join when the process-directed watchdog signal is delivered.
class GuestJoinSignalMask {
    sigset_t previous_{};
    bool changed_=false;
public:
    GuestJoinSignalMask() noexcept{
        sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGALRM);
        changed_=pthread_sigmask(SIG_BLOCK,&mask,&previous_)==0;
    }
    ~GuestJoinSignalMask(){if(changed_)pthread_sigmask(SIG_SETMASK,&previous_,nullptr);}
    GuestJoinSignalMask(const GuestJoinSignalMask&)=delete;
    GuestJoinSignalMask& operator=(const GuestJoinSignalMask&)=delete;
};
class GuestFaultStack {
    alignas(16) unsigned char storage_[65536];
    stack_t previous_{};
    bool installed_=false;
public:
    GuestFaultStack() noexcept {
        stack_t stack{};
        stack.ss_sp=storage_;stack.ss_size=sizeof(storage_);
        installed_=sigaltstack(&stack,&previous_)==0;
    }
    ~GuestFaultStack(){if(installed_) (void)sigaltstack(&previous_,nullptr);}
    GuestFaultStack(const GuestFaultStack&)=delete;
    GuestFaultStack& operator=(const GuestFaultStack&)=delete;
};
}
