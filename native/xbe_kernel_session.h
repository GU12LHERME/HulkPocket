#pragma once
#include "xbe_kernel_pool_calls.h"
#include "xbe_kernel_thread.h"
#include "xbe_tls.h"
#include "hulk_kernel_clock.h"
#include "xbe_guest_thread_exit.h"
#include "xbe_kernel_rtl.h"
#include "xbe_kernel_virtual_memory.h"
#include <map>
#include <chrono>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
namespace hulkpocket {
// Bounded diagnostic scheduler. Suspension is cooperative at kernel calls;
// the process watchdog remains responsible for non-cooperative guest code.
class XbeKernelSession {
public:
    using Callback=void(*)(X86Context&,uint8_t*,uint32_t);
    using Entry=int(*)(X86Context*,uint8_t*,uint32_t,Callback);
private:
    struct Worker {
        std::thread host;
        uint32_t suspend=0, pcr=0, handle=0, id=0;
        bool finished=false, closed=false;
    };
    XboxGuestAddressSpace& mem_;
    XbeKernelPool pool_;
    XbeVirtualMemory virtual_;
    XbeTlsTemplate tls_;
    const HulkKernelClock* clock_;
    std::mutex lock_;
    std::condition_variable cv_;
    std::array<Worker,16> workers_{};
    size_t count_=0;
    struct Event { uint32_t body; bool manual; };
    std::map<uint32_t,Event> events_;
    uint32_t next_event_=0x2000;
    std::map<uint32_t,uint32_t> critical_;
    bool stopping_=false;
    int worker_failure_=0;
    Entry entry_=nullptr;
    inline static thread_local XbeKernelSession* active_=nullptr;
    Worker* Find(uint32_t handle) {
        for(size_t i=0;i<count_;++i)
            if(workers_[i].handle==handle && !workers_[i].closed) return &workers_[i];
        return nullptr;
    }
    void Pause(X86Context& cpu,std::unique_lock<std::mutex>& lock) {
        Worker* w=nullptr;
        for(size_t i=0;i<count_;++i) if(workers_[i].handle==cpu.thread_handle) w=&workers_[i];
        cv_.wait(lock,[&]{return stopping_ || !w || !w->suspend;});
        if(stopping_) throw std::runtime_error("Diagnostic session stopping");
    }
    uint32_t Arg(const X86Context& cpu,unsigned i) {
        return mem_.Read32(cpu.esp+4+4*i);
    }
    void Check(const X86Context& cpu,unsigned n) {
        if(uint64_t(cpu.esp)+4+4*n>=XboxGuestAddressSpace::kSize ||
           !mem_.IsCommitted(cpu.esp,4+4*n))
            throw std::runtime_error("Invalid kernel call stack");
    }
    static void Ret(X86Context& cpu,unsigned n,uint32_t value) {
        cpu.eax=value; cpu.esp+=4+4*n;
    }
    void Output(uint32_t addr,uint32_t value) {
        if(addr) mem_.Write32(addr,value);
    }
    void CreateThread(X86Context& cpu,uint32_t ordinal) {
        const unsigned n=ordinal==255?10:5;
        Check(cpu,n);
        const uint32_t out=Arg(cpu,0), idout=Arg(cpu,ordinal==255?4:1);
        const uint32_t start=Arg(cpu,ordinal==255?5:2), context=Arg(cpu,ordinal==255?6:3);
        const uint32_t system=ordinal==255?Arg(cpu,9):0;
        const uint32_t requested=ordinal==255?Arg(cpu,2):0;
        const uint32_t requested_tls=ordinal==255?Arg(cpu,3):0;
        const uint32_t template_size=tls_.end-tls_.start+tls_.zero_fill;
        const uint32_t aligned_tls=(template_size+3u)&~3u;
        if(!entry_ || !start || count_>=workers_.size() || requested>0x80000 ||
           aligned_tls>0x8000 || (requested_tls && ((uint64_t(requested_tls)+3)&~uint64_t(3))!=aligned_tls))
            throw std::runtime_error("Unsupported diagnostic thread stack/TLS or capacity");
        for(auto addr:{out,idout})
            if(addr && !mem_.IsCommitted(addr,4)) throw std::runtime_error("Invalid thread output");
        const uint32_t slot=0x0A000000u+static_cast<uint32_t>(count_)*0x100000u;
        const uint32_t handle=0x100u+static_cast<uint32_t>(count_)*4u;
        const uint32_t id=0x1000u+static_cast<uint32_t>(count_)*4u;
        const uint32_t top=slot+0xF0000, stack=slot+0x60000;
        if(mem_.IsCommitted(slot,mem_.PageSize()) || mem_.IsCommitted(stack,top-stack))
            throw std::runtime_error("Thread arena occupied");
        mem_.Commit(slot,0x1000);
        mem_.Commit(stack,top-stack);
        const uint32_t tls=top-aligned_tls;
        if(template_size) CopyXbeTls(tls_,mem_,tls);
        X86Context child{};
        child.esp=(tls&~15u)-16;
        child.fs_base=slot; child.thread_handle=handle;
        child.fcw=0x027f; child.mxcsr=0x1f80;
        mem_.Write32(child.esp,0);
        mem_.Write32(child.esp+4,system?start:context);
        if(system) mem_.Write32(child.esp+8,context);
        InitializeKernelThread(mem_,slot,slot+0x400,top,stack,child.esp,
                               template_size?tls:0,id);
        auto& w=workers_[count_++];
        w.pcr=slot; w.handle=handle; w.id=id;
        w.suspend=ordinal==255 && (Arg(cpu,7)&255u)?1u:0u;
        const auto target=system?system:start;
        w.host=std::thread([this,&w,child,target]() mutable {
            active_=this;
            int result=0;
            try {
                { std::unique_lock<std::mutex> lock(lock_); Pause(child,lock); }
                result=entry_(&child,mem_.Base(),target,Dispatch);
            } catch(const GuestThreadExit& e) {
                child.eax=e.code;
            } catch(const std::exception& e) {
                std::fprintf(stderr,"Guest thread failed: %s\n",e.what()); result=1;
            } catch(...) { result=1; }
            active_=nullptr;
            std::lock_guard<std::mutex> lock(lock_);
            w.finished=true;
            mem_.Write32(w.pcr+0x404,1); // dispatcher SignalState
            if(result && !stopping_) worker_failure_=result;
            cv_.notify_all();
        });
        Output(out,handle); Output(idout,id);
        std::fprintf(stderr,"Created diagnostic guest thread %X (id %X) at %08X\n",handle,id,target);
        Ret(cpu,n,0);
    }
    // Xbox LARGE_INTEGER relative time is negative in 100 ns units.
    using Steady=std::chrono::steady_clock;
    static int64_t Now100ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()/100
            +116444736000000000LL;
    }
    Steady::time_point Deadline(uint32_t addr) {
        if(!addr) return Steady::time_point::max();
        int64_t ticks=0; mem_.Read(addr,&ticks,8);
        // Bound diagnostic waits to 15 seconds; the outer process has its own watchdog.
        uint64_t delta=ticks<0?uint64_t(-(ticks+1))+1:
            ticks>Now100ns()?uint64_t(ticks-Now100ns()):0;
        return Steady::now()+std::chrono::nanoseconds(std::min<uint64_t>(delta,150000000)*100);
    }
    uint32_t Body(uint32_t h) {
        auto it=events_.find(h);
        if(it!=events_.end()) return it->second.body;
        auto* w=Find(h);
        if(w) return w->pcr+0x400;
        return 0;
    }
    uint32_t WaitBody(X86Context& cpu,uint32_t body,uint32_t timeout,
                      std::unique_lock<std::mutex>& lock) {
        if(!body) return 0xc0000008u;
        const auto deadline=Deadline(timeout);
        const auto ready=[&]{return stopping_ || mem_.Read32(body+4)!=0;};
        do {
            if(!cv_.wait_until(lock,deadline,ready)) return 0x102;
            Pause(cpu,lock);
        } while(!mem_.Read32(body+4));
        // Synchronization event is consumed by exactly one waiter.
        if((mem_.Read32(body)&255u)==1) mem_.Write32(body+4,0);
        return 0;
    }
    void Critical(X86Context& cpu,uint32_t ordinal,std::unique_lock<std::mutex>& lock) {
        Check(cpu,1); const auto addr=Arg(cpu,0);
        if(!mem_.IsCommitted(addr,28)) throw std::runtime_error("Invalid critical section");
        if(ordinal==291) {
            if(critical_.count(addr) && mem_.Read32(addr+0x18))
                throw std::runtime_error("Reinitializing held critical section");
            mem_.Write32(addr,0x00040001);
            mem_.Write32(addr+4,0); mem_.Write32(addr+8,addr+8); mem_.Write32(addr+12,addr+8);
            mem_.Write32(addr+16,0xffffffff); mem_.Write32(addr+20,0); mem_.Write32(addr+24,0);
            critical_[addr]=0; Ret(cpu,1,0); return;
        }
        if(!critical_.count(addr)) throw std::runtime_error("Uninitialized critical section");
        auto me=mem_.Read32(cpu.fs_base+0x28);
        if(!me) throw std::runtime_error("Missing current thread");
        if(ordinal==277) {
            ++critical_[addr];
            do {
                cv_.wait(lock,[&]{auto owner=mem_.Read32(addr+24);return stopping_ || !owner || owner==me;});
                Pause(cpu,lock);
            } while(mem_.Read32(addr+24) && mem_.Read32(addr+24)!=me);
            --critical_[addr];
            mem_.Write32(addr+24,me);
            mem_.Write32(addr+20,mem_.Read32(addr+20)+1);
        } else {
            if(mem_.Read32(addr+24)!=me || !mem_.Read32(addr+20))
                throw std::runtime_error("Critical section not owned by current thread");
            auto depth=mem_.Read32(addr+20)-1;
            mem_.Write32(addr+20,depth);
            if(!depth) {mem_.Write32(addr+24,0);cv_.notify_all();}
        }
        mem_.Write32(addr+16,mem_.Read32(addr+20)-1);
        Ret(cpu,1,0);
    }
    void Call(X86Context& cpu,uint32_t ordinal,std::unique_lock<std::mutex>& lock) {
        Pause(cpu,lock);
        if(clock_) clock_->Update(mem_);
        switch(ordinal) {
            case 184: case 199: {
                unsigned n=ordinal==184?5:3;Check(cpu,n);
                auto bp=Arg(cpu,0), sp=Arg(cpu,ordinal==184?2:1);
                if(!mem_.IsCommitted(bp,4)||!mem_.IsCommitted(sp,4)||bp==sp) {
                    Ret(cpu,n,0xc000000du);return;
                }
                uint32_t b=mem_.Read32(bp), z=mem_.Read32(sp);
                // Do not accept guest pointers inside the allocation being released.
                if(ordinal==199 && b>=0x10000000 && b<0x30000000 &&
                   ((bp>=0x10000000 && bp<0x30000000)||(sp>=0x10000000 && sp<0x30000000)))
                    throw std::runtime_error("Virtual free output inside virtual arena");
                auto status=ordinal==184?
                    (Arg(cpu,1)?0xc000000du:virtual_.Allocate(b,z,Arg(cpu,3),Arg(cpu,4))):
                    virtual_.Free(b,z,Arg(cpu,2));
                if(!status){mem_.Write32(bp,b);mem_.Write32(sp,z);}
                Ret(cpu,n,status);return;
            }
            case 217: Check(cpu,2);Ret(cpu,2,virtual_.Query(Arg(cpu,0),Arg(cpu,1)));return;
            case 180: Check(cpu,1);Ret(cpu,1,virtual_.AllocationSize(Arg(cpu,0)));return;
            case 291: case 277: case 294: Critical(cpu,ordinal,lock); return;
            case 289: Check(cpu,2); InitGuestAnsiString(mem_,Arg(cpu,0),Arg(cpu,1)); Ret(cpu,2,0); return;
            case 270: case 279: {
                Check(cpu,3);
                auto v=CompareGuestAnsiStrings(mem_,Arg(cpu,0),Arg(cpu,1),(Arg(cpu,2)&255u)!=0);
                Ret(cpu,3,ordinal==279?uint32_t(v==0):uint32_t(v)); return;
            }
            case 128: {
                Check(cpu,1); const auto now=Now100ns(); mem_.Write(Arg(cpu,0),&now,8);
                Ret(cpu,1,0); return;
            }
            case 99: {
                Check(cpu,3);
                if((Arg(cpu,1)&255u)||!Arg(cpu,2)) throw std::runtime_error("Unsupported alertable delay or missing interval");
                auto deadline=Deadline(Arg(cpu,2));
                cv_.wait_until(lock,deadline,[&]{return stopping_;});
                Pause(cpu,lock); Ret(cpu,3,0); return;
            }
            case 189: {
                Check(cpu,4);
                auto out=Arg(cpu,0), attrs=Arg(cpu,1), type=Arg(cpu,2);
                if(!mem_.IsCommitted(out,4) || type>1) {Ret(cpu,4,0xc000000du);return;}
                // Only unnamed events; reject name/attribute semantics explicitly.
                if(attrs) throw std::runtime_error("Named/attributed event not implemented");
                auto body=pool_.Allocate(16);
                if(!body) {Ret(cpu,4,0xc0000017u);return;}
                auto h=next_event_;next_event_+=4;
                mem_.Write32(body,type|0x00040000u);
                mem_.Write32(body+4,(Arg(cpu,3)&255u)?1:0);
                mem_.Write32(body+8,body+8);mem_.Write32(body+12,body+8);
                events_.emplace(h,Event{body,type==0});Output(out,h);
                Ret(cpu,4,0);return;
            }
            case 225: case 186: {
                const unsigned n=ordinal==225?2:1;Check(cpu,n);
                auto it=events_.find(Arg(cpu,0));
                if(it==events_.end()){Ret(cpu,n,0xc0000008u);return;}
                auto body=it->second.body;
                if(ordinal==225) Output(Arg(cpu,1),mem_.Read32(body+4));
                mem_.Write32(body+4,ordinal==225?1:0);
                cv_.notify_all();Ret(cpu,n,0);return;
            }
            case 233: case 234: {
                unsigned n=ordinal==233?3:4;Check(cpu,n);
                if(Arg(cpu,n-2)&255u) throw std::runtime_error("Alertable wait not implemented");
                auto status=WaitBody(cpu,Body(Arg(cpu,0)),Arg(cpu,n-1),lock);
                Ret(cpu,n,status);return;
            }
            case 187: {
                Check(cpu,1);auto h=Arg(cpu,0);
                auto it=events_.find(h);
                // Keep event body alive until session end (in-flight waits may refer to it).
                if(it!=events_.end()){events_.erase(it);Ret(cpu,1,0);return;}
                if(auto* w=Find(h)){w->closed=true;Ret(cpu,1,0);return;}
                Ret(cpu,1,0xc0000008u);return;
            }
            case 246: {
                Check(cpu,3);auto h=Arg(cpu,0);
                auto body=h==0xfffffffeu?mem_.Read32(cpu.fs_base+0x28):Body(h);
                if(!body){Ret(cpu,3,0xc0000008u);return;}
                if(Arg(cpu,1)) throw std::runtime_error("Typed object reference not implemented");
                Output(Arg(cpu,2),body);Ret(cpu,3,0);return;
            }
            case 254: case 255: CreateThread(cpu,ordinal); return;
            case 224: case 231: {
                Check(cpu,2);
                auto* w=Find(Arg(cpu,0));
                if(!w || w->finished) { Ret(cpu,2,0xC0000008u); return; }
                const auto out=Arg(cpu,1);
                if(out && !mem_.IsCommitted(out,4)) throw std::runtime_error("Invalid suspend output");
                const auto prev=w->suspend;
                if(ordinal==231) {
                    if(prev==127) { Ret(cpu,2,0xC000004Au); return; }
                    ++w->suspend;
                } else if(prev) --w->suspend;
                Output(out,prev); Ret(cpu,2,0);
                cv_.notify_all(); Pause(cpu,lock); return;
            }
            case 104: Check(cpu,0); Ret(cpu,0,mem_.Read32(cpu.fs_base+0x28)); return;
            case 238: Check(cpu,0); Ret(cpu,0,0); lock.unlock(); std::this_thread::yield(); return;
            case 258: Check(cpu,1); throw GuestThreadExit{Arg(cpu,0)};
            default: break;
        }
        if(!DispatchPoolCall(ordinal,cpu,mem_,pool_))
            throw std::runtime_error("Unsupported kernel ordinal "+std::to_string(ordinal));
    }
    void Join() {
        for(size_t i=0;;++i) {
            std::thread worker;
            { std::lock_guard<std::mutex> lock(lock_);
              if(i>=count_) break;
              worker=std::move(workers_[i].host); }
            if(worker.joinable()) worker.join();
        }
    }
    void Stop() {
        { std::lock_guard<std::mutex> lock(lock_); stopping_=true; cv_.notify_all(); }
        Join();
    }
public:
    XbeKernelSession(XboxGuestAddressSpace& mem,uint32_t base,uint32_t size,
                     XbeTlsTemplate tls={},const HulkKernelClock* clock=nullptr)
        :mem_(mem),pool_(mem,base,size),virtual_(mem),tls_(tls),clock_(clock) {}
    XbeKernelSession(const XbeKernelSession&)=delete;
    XbeKernelSession& operator=(const XbeKernelSession&)=delete;
    ~XbeKernelSession() { Stop(); }
    static void Dispatch(X86Context& cpu,uint8_t* base,uint32_t ordinal) {
        if(!active_ || active_->mem_.Base()!=base)
            throw std::runtime_error("Invalid kernel session");
        std::unique_lock<std::mutex> lock(active_->lock_);
        active_->Call(cpu,ordinal,lock);
    }
    int Run(Entry entry,X86Context& cpu,uint32_t address) {
        if(!entry || active_ || entry_) throw std::runtime_error("Invalid kernel session");
        struct Reset { XbeKernelSession& s; ~Reset(){s.entry_=nullptr; active_=nullptr;} } reset{*this};
        stopping_=false; worker_failure_=0;
        active_=this; entry_=entry;
        int result=0;
        try { result=entry(&cpu,mem_.Base(),address,Dispatch); }
        catch(const GuestThreadExit& e) { cpu.eax=e.code; }
        catch(...) { Stop(); throw; }
        if(result) Stop(); else Join();
        return result?result:worker_failure_;
    }
};
}
