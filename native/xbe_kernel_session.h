#pragma once
#include "xbe_kernel_pool_calls.h"
#include "xbe_kernel_thread.h"
#include "xbe_tls.h"
#include "hulk_kernel_clock.h"
#include "xbe_guest_thread_exit.h"
#include "xbe_kernel_rtl.h"
#include "xbe_kernel_crypto.h"
#include "xbe_kernel_virtual_memory.h"
#include "xbe_kernel_physical_memory.h"
#include "xbe_kernel_files.h"
#include "xbe_fault_diagnostic.h"
#include "xbe_graphics_boundary.h"
#include "xbe_kernel_calendar.h"
#include <map>
#include <deque>
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
    XbePhysicalMemory physical_;
    XbeDiscFiles files_;
    XbeTlsTemplate tls_;
    const HulkKernelClock* clock_;
    const HulkKernelDataRegistry* registry_;
    std::mutex lock_;
    std::condition_variable cv_;
    std::array<Worker,16> workers_{};
    size_t count_=0;
    struct Event { uint32_t body; bool manual; };
    std::map<uint32_t,uint32_t> semaphore_limits_;
    std::map<uint32_t,Event> events_;
    uint32_t next_event_=0x2000;
    std::map<uint32_t,uint32_t> critical_;
    std::map<uint32_t,uint32_t> shutdown_notifications_;
    struct Timer {std::chrono::steady_clock::time_point due;uint32_t dpc;uint32_t period;};
    std::map<uint32_t,Timer> timers_;
    std::map<uint32_t,uint32_t> timer_types_;
    std::map<uint32_t,uint32_t> pointer_events_;
    std::map<uint32_t,uint32_t> dpc_routines_;
    std::deque<uint32_t> dpc_queue_;
    bool dpc_running_=false;
    uint32_t cache_pages_=16;
    const std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
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
        const uint64_t template_size=uint64_t(tls_.end)-tls_.start+tls_.zero_fill;
        const uint64_t default_tls=template_size?((template_size+15)&~uint64_t(15))+4:0;
        const uint64_t requested_aligned=(uint64_t(requested_tls)+3)&~uint64_t(3);
        // XAPI can request extra per-thread TLS. Keep enough space for both
        // the image template and the requested allocation, without changing
        // the image's global TLS index from a secondary thread.
        const uint64_t tls_bytes=std::max(default_tls,requested_aligned);
        const uint64_t stack_bytes=std::max<uint64_t>(requested,0x10000);
        const auto page=uint64_t(mem_.PageSize());
        const uint64_t committed=(stack_bytes+tls_bytes+32+page-1)&~(page-1);
        std::fprintf(stderr,"PsCreateSystemThread%s: stack=%08X tls=%08X template=%llu allocated_tls=%llu start=%08X system=%08X suspend=%u workers=%zu\n",
            ordinal==255?"Ex":"",requested,requested_tls,
            static_cast<unsigned long long>(template_size),static_cast<unsigned long long>(tls_bytes),
            start,system,ordinal==255?(Arg(cpu,7)&255u):0,count_);
        if(!entry_ || (!start && !system)) throw std::runtime_error("Invalid guest thread entry");
        if(count_>=workers_.size()) throw std::runtime_error("Diagnostic thread capacity exhausted");
        if(tls_bytes>0x10000 || committed>0x3D0000 || tls_.end<tls_.start)
            throw std::runtime_error("Diagnostic thread stack/TLS exceeds reserved arena");
        for(auto addr:{out,idout})
            if(addr && !mem_.IsCommitted(addr,4)) throw std::runtime_error("Invalid thread output");
        const uint32_t slot=0x0A000000u+static_cast<uint32_t>(count_)*0x400000u;
        const uint32_t handle=0x100u+static_cast<uint32_t>(count_)*4u;
        const uint32_t id=0x1000u+static_cast<uint32_t>(count_)*4u;
        const uint32_t top=slot+0x3F0000, stack=top-static_cast<uint32_t>(committed);
        if(mem_.IsCommitted(slot,mem_.PageSize()) || mem_.IsCommitted(stack,top-stack))
            throw std::runtime_error("Thread arena occupied");
        mem_.Commit(slot,mem_.PageSize());
        mem_.Commit(stack,top-stack);
        const uint32_t tls=top-static_cast<uint32_t>(tls_bytes);
        if(template_size) CopyXbeTls(tls_,mem_,tls);
        X86Context child{};
        child.esp=(tls&~15u)-16;
        child.fs_base=slot; child.thread_handle=handle;
        child.fcw=0x027f; child.mxcsr=0x1f80;
        mem_.Write32(child.esp,0);
        mem_.Write32(child.esp+4,system?start:context);
        if(system) mem_.Write32(child.esp+8,context);
        InitializeKernelThread(mem_,slot,slot+0x400,top,stack,child.esp,
                               tls_bytes?tls:0,id);
        auto& w=workers_[count_++];
        w.pcr=slot; w.handle=handle; w.id=id;
        w.suspend=ordinal==255 && (Arg(cpu,7)&255u)?1u:0u;
        const auto target=system?system:start;
        w.host=std::thread([this,&w,child,target]() mutable {
            GuestFaultStack fault_stack_scope;
            fault_thread=static_cast<sig_atomic_t>(child.thread_handle);
            fault_ordinal=0;fault_return=0;fault_stack=static_cast<sig_atomic_t>(child.esp);
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
            if(result && !stopping_){worker_failure_=result;stopping_=true;}
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
    void InitDispatcher(uint32_t body,uint32_t size,uint32_t type,uint32_t signal){
        if(!body||body%4||!mem_.IsCommitted(body,size))throw std::runtime_error("Invalid dispatcher object");
        std::array<uint8_t,40> zero{};
        mem_.Write(body,zero.data(),size);
        mem_.Write32(body,type|((size/4)<<16));
        mem_.Write32(body+4,signal);mem_.Write32(body+8,body+8);mem_.Write32(body+12,body+8);
    }
    bool ValidDpc(uint32_t d){
        return dpc_routines_.count(d)&&mem_.IsCommitted(d,28)&&
            (mem_.Read32(d)&0xffffu)==19&&mem_.Read32(d+12)==dpc_routines_.at(d);
    }
    bool QueueDpc(uint32_t d,uint32_t a,uint32_t b){
        if(!ValidDpc(d))throw std::runtime_error("Invalid initialized DPC");
        if(mem_.Read32(d)&0x10000u)return false;
        if(dpc_queue_.size()>=256)throw std::runtime_error("DPC queue capacity exhausted");
        mem_.Write32(d,mem_.Read32(d)|0x10000u);
        mem_.Write32(d+20,a);mem_.Write32(d+24,b);
        dpc_queue_.push_back(d);cv_.notify_all();return true;
    }
    void ServiceTimers(){
        auto now=Steady::now();
        for(auto it=timers_.begin();it!=timers_.end();){
            auto body=it->first;auto& timer=it->second;
            if(timer.due>now){++it;continue;}
            if(!mem_.IsCommitted(body,40))throw std::runtime_error("Invalid active timer storage");
            mem_.Write32(body+4,1);
            if(timer.dpc){
                auto ticks=uint64_t(Now100ns());
                QueueDpc(timer.dpc,uint32_t(ticks),uint32_t(ticks>>32));
            }
            cv_.notify_all();
            if(timer.period){
                timer.due=now+std::chrono::milliseconds(timer.period);++it;
            }else{
                mem_.Write32(body,mem_.Read32(body)&~0x01000000u);
                it=timers_.erase(it);
            }
        }
    }
    Steady::time_point NextTimer(Steady::time_point deadline){
        for(const auto& item:timers_)deadline=std::min(deadline,item.second.due);
        return deadline;
    }
    // Cooperative DPC delivery: execute one guest callback at a kernel safe
    // point, on the current guest context's stack, with isolated registers.
    // This is not a separate Xbox interrupt thread or an IRQL emulation.
    void ServiceDpc(X86Context& cpu,std::unique_lock<std::mutex>& lock){
        ServiceTimers();
        if(dpc_running_||dpc_queue_.empty())return;
        auto d=dpc_queue_.front();
        if(!ValidDpc(d))throw std::runtime_error("Invalid queued DPC storage");
        auto routine=mem_.Read32(d+12),context=mem_.Read32(d+16),
            a=mem_.Read32(d+20),b=mem_.Read32(d+24);
        if(!entry_||cpu.esp<20||!mem_.IsCommitted(cpu.esp-20,20))
            throw std::runtime_error("No DPC callback stack");
        dpc_queue_.pop_front();mem_.Write32(d,mem_.Read32(d)&~0x10000u);
        X86Context nested=cpu;
        nested.esp-=20;
        const uint32_t args[]={0,d,context,a,b};
        mem_.Write(nested.esp,args,sizeof(args));
        dpc_running_=true;
        auto old_ordinal=fault_ordinal,old_return=fault_return,old_stack=fault_stack;
        lock.unlock();
        int status=0;
        try{status=entry_(&nested,mem_.Base(),routine,Dispatch);}
        catch(...){lock.lock();dpc_running_=false;cv_.notify_all();throw;}
        lock.lock();dpc_running_=false;cv_.notify_all();
        fault_ordinal=old_ordinal;fault_return=old_return;fault_stack=old_stack;
        if(status||nested.esp!=cpu.esp)throw std::runtime_error("DPC guest callback failed or violated stack ABI");
    }
    bool PointerObject(uint32_t body){
        if(!body||!mem_.IsCommitted(body,16))return false;
        uint32_t header=mem_.Read32(body),type=header&255u;
        if(timer_types_.count(body))return type==8+timer_types_.at(body);
        if(pointer_events_.count(body))return type==pointer_events_.at(body);
        for(const auto& item:events_)if(item.second.body==body)return true;
        for(size_t i=0;i<count_;++i)if(workers_[i].pcr+0x400==body)return true;
        return false;
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
        for(;;){
            Pause(cpu,lock);ServiceDpc(cpu,lock);
            if(mem_.Read32(body+4))break;
            if(Steady::now()>=deadline)return 0x102;
            if(!dpc_running_&&!dpc_queue_.empty())continue;
            cv_.wait_until(lock,NextTimer(deadline));
        }
        // Synchronization event is consumed by exactly one waiter.
        const auto type=mem_.Read32(body)&255u;
        if(type==1||type==9) mem_.Write32(body+4,0);
        else if(type==5) mem_.Write32(body+4,mem_.Read32(body+4)-1);
        return 0;
    }
    void Critical(X86Context& cpu,uint32_t ordinal,std::unique_lock<std::mutex>& lock) {
        Check(cpu,1); const auto addr=Arg(cpu,0);
        if(addr%4 || !mem_.IsCommitted(addr,28)) throw std::runtime_error("Invalid critical section pointer");
        auto initialize=[&]{
            mem_.Write32(addr,0x00040001);
            mem_.Write32(addr+4,0); mem_.Write32(addr+8,addr+8); mem_.Write32(addr+12,addr+8);
            mem_.Write32(addr+16,0xffffffff); mem_.Write32(addr+20,0); mem_.Write32(addr+24,0);
            critical_[addr]=0;
        };
        if(ordinal==291) {
            if(critical_.count(addr) && (mem_.Read32(addr+24)||critical_.at(addr)))
                throw std::runtime_error("Reinitializing held critical section");
            initialize();Ret(cpu,1,0);return;
        }
        if(!critical_.count(addr)) {
            const auto header=mem_.Read32(addr), signal=mem_.Read32(addr+4);
            const auto fl=mem_.Read32(addr+8), bl=mem_.Read32(addr+12);
            const auto count=mem_.Read32(addr+16), depth=mem_.Read32(addr+20), owner=mem_.Read32(addr+24);
            std::fprintf(stderr,"Critical section first use: ordinal=%u addr=%08X header=%08X signal=%08X list=%08X/%08X lock=%08X depth=%u owner=%08X\n",
                ordinal,addr,header,signal,fl,bl,count,depth,owner);
            // XAPI/CRT may initialize the object inline, without invoking ordinal 291.
            // Also accept a zero/static idle representation on first enter only.
            const bool empty_header=!header&&!signal&&!fl&&!bl;
            const bool idle=!owner&&!depth&&(count==0xffffffffu||count==0);
            if(ordinal==277 && empty_header && idle) initialize();
            else if((header&0x00ff00ffu)==0x00040001u && signal<=1 &&
                    fl==addr+8 && bl==addr+8 && ((!depth&&!owner&&count==0xffffffffu) ||
                    (depth&&owner&&count==depth-1))) {
                critical_[addr]=0;
            } else throw std::runtime_error("Unrecognized guest critical section layout");
        }
        auto me=mem_.Read32(cpu.fs_base+0x28);
        if(!me) throw std::runtime_error("Missing current thread");
        auto update=[&]{
            mem_.Write32(addr+16,mem_.Read32(addr+20)+critical_.at(addr)-1);
        };
        if(ordinal==277) {
            ++critical_[addr];update();
            do {
                cv_.wait(lock,[&]{auto owner=mem_.Read32(addr+24);return stopping_ || !owner || owner==me;});
                Pause(cpu,lock);
            } while(mem_.Read32(addr+24) && mem_.Read32(addr+24)!=me);
            --critical_[addr];
            mem_.Write32(addr+4,0);
            mem_.Write32(addr+24,me);
            mem_.Write32(addr+20,mem_.Read32(addr+20)+1);
        } else {
            if(mem_.Read32(addr+24)!=me || !mem_.Read32(addr+20))
                throw std::runtime_error("Critical section not owned by current thread");
            auto depth=mem_.Read32(addr+20)-1;
            mem_.Write32(addr+20,depth);
            if(!depth) {
                mem_.Write32(addr+24,0);
                mem_.Write32(addr+4,critical_.at(addr)?1:0);
                cv_.notify_all();
            }
        }
        update();Ret(cpu,1,0);
    }
    void Call(X86Context& cpu,uint32_t ordinal,std::unique_lock<std::mutex>& lock) {
        Pause(cpu,lock);
        if(clock_) clock_->Update(mem_);
        switch(ordinal) {
            case 107: {
                Check(cpu,3);auto d=Arg(cpu,0),routine=Arg(cpu,1),context=Arg(cpu,2);
                if(!d||d%4||!mem_.IsCommitted(d,28)||!routine||!mem_.IsCommitted(routine,1))
                    throw std::runtime_error("Invalid DPC initialization");
                if(dpc_routines_.count(d)&&(mem_.Read32(d)&0x10000u))
                    throw std::runtime_error("Reinitializing queued DPC");
                if(dpc_routines_.size()>=256&&!dpc_routines_.count(d))
                    throw std::runtime_error("DPC capacity exhausted");
                std::array<uint8_t,28> zero{};mem_.Write(d,zero.data(),zero.size());
                mem_.Write32(d,19);mem_.Write32(d+12,routine);mem_.Write32(d+16,context);
                dpc_routines_[d]=routine;
                std::fprintf(stderr,"Initialized DPC %08X routine=%08X context=%08X\n",d,routine,context);
                Ret(cpu,3,0);return;
            }
            case 119: {
                Check(cpu,3);auto queued=QueueDpc(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2));
                Ret(cpu,3,queued);return;
            }
            case 137: {
                Check(cpu,1);auto d=Arg(cpu,0);
                if(!ValidDpc(d))throw std::runtime_error("Invalid DPC removal");
                auto it=std::find(dpc_queue_.begin(),dpc_queue_.end(),d);
                bool queued=it!=dpc_queue_.end();
                if(queued){dpc_queue_.erase(it);mem_.Write32(d,mem_.Read32(d)&~0x10000u);}
                Ret(cpu,1,queued);return;
            }
            case 113: {
                Check(cpu,2);auto body=Arg(cpu,0),type=Arg(cpu,1);
                if(type>1||timers_.count(body))throw std::runtime_error("Invalid timer initialization");
                if(timer_types_.size()>=256&&!timer_types_.count(body))throw std::runtime_error("Timer capacity exhausted");
                InitDispatcher(body,40,8+type,0);timer_types_[body]=type;
                Ret(cpu,2,0);return;
            }
            case 149: case 150: {
                unsigned n=ordinal==149?4:5;Check(cpu,n);
                auto body=Arg(cpu,0),dpc=Arg(cpu,n-1),period=ordinal==150?Arg(cpu,3):0;
                if(!timer_types_.count(body)||!PointerObject(body)||(dpc&&!ValidDpc(dpc))||period>86400000)
                    throw std::runtime_error("Invalid timer scheduling");
                uint64_t raw=uint64_t(Arg(cpu,1))|(uint64_t(Arg(cpu,2))<<32);
                int64_t ticks;std::memcpy(&ticks,&raw,8);
                auto system_now=Now100ns();
                uint64_t delta=ticks<0?uint64_t(-(ticks+1))+1:ticks>system_now?uint64_t(ticks-system_now):0;
                if(delta>864000000000ull)throw std::runtime_error("Timer due exceeds diagnostic budget");
                bool previous=timers_.count(body);
                auto due=Steady::now()+std::chrono::nanoseconds(delta*100);
                timers_[body]={due,dpc,period};
                mem_.Write32(body,mem_.Read32(body)|0x01000000u);mem_.Write32(body+4,0);
                mem_.Write(body+16,&raw,8);mem_.Write32(body+32,dpc);mem_.Write32(body+36,period);
                cv_.notify_all();Ret(cpu,n,previous);return;
            }
            case 97: {
                Check(cpu,1);auto body=Arg(cpu,0);
                if(!timer_types_.count(body)||!PointerObject(body))throw std::runtime_error("Invalid timer cancellation");
                bool previous=timers_.erase(body)!=0;
                mem_.Write32(body,mem_.Read32(body)&~0x01000000u);
                cv_.notify_all();Ret(cpu,1,previous);return;
            }
            case 108: {
                Check(cpu,3);auto body=Arg(cpu,0),type=Arg(cpu,1);
                if(type>1)throw std::runtime_error("Invalid event type");
                if(pointer_events_.size()>=256&&!pointer_events_.count(body))throw std::runtime_error("Event capacity exhausted");
                InitDispatcher(body,16,type,(Arg(cpu,2)&255u)?1:0);
                pointer_events_[body]=type;Ret(cpu,3,0);return;
            }
            case 145: case 138: {
                unsigned n=ordinal==145?3:1;Check(cpu,n);auto body=Arg(cpu,0);
                if(!PointerObject(body)||(mem_.Read32(body)&255u)>1)
                    throw std::runtime_error("Invalid direct event object");
                if(ordinal==145&&(Arg(cpu,2)&255u))throw std::runtime_error("KeSetEvent wait handoff not implemented");
                auto old=mem_.Read32(body+4);mem_.Write32(body+4,ordinal==145?1:0);
                cv_.notify_all();Ret(cpu,n,old);return;
            }
            case 159: {
                Check(cpu,5);
                if(Arg(cpu,3)&255u)throw std::runtime_error("Alertable pointer wait not implemented");
                auto body=Arg(cpu,0);
                if(!PointerObject(body))throw std::runtime_error("Unsupported dispatcher pointer wait");
                auto result=WaitBody(cpu,body,Arg(cpu,4),lock);Ret(cpu,5,result);return;
            }
            case kGraphicsCreate: {
                Check(cpu,6);
                auto pp=Arg(cpu,4);
                std::fprintf(stderr,"Direct3D_CreateDevice: adapter=%08X type=%08X window=%08X behavior=%08X pp=%08X out=%08X\n",
                    Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),pp,Arg(cpu,5));
                if(!mem_.IsCommitted(pp,0x34))throw std::runtime_error("Invalid D3D present parameters");
                std::fprintf(stderr,"D3D8 REQUEST: width=%u height=%u format=%X buffers=%u multisample=%X depth=%u depth_format=%X interval=%X\n",
                    mem_.Read32(pp),mem_.Read32(pp+4),mem_.Read32(pp+8),mem_.Read32(pp+12),mem_.Read32(pp+16),
                    mem_.Read32(pp+0x20),mem_.Read32(pp+0x24),mem_.Read32(pp+0x30));
                throw std::runtime_error("D3D8 CreateDevice reached; guest device initialization pending");
            }
            case kGraphicsDraw: case kGraphicsDrawIndexed: case kGraphicsSwap: {
                Check(cpu,ordinal==kGraphicsSwap?1:3);
                std::fprintf(stderr,"D3D8 draw/swap boundary id=%X first=%08X\n",ordinal,Arg(cpu,0));
                throw std::runtime_error("D3D8 draw/swap guest integration pending");
            }
            case 125: case 126: case 127: {
                Check(cpu,0);
                uint64_t ns=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now()-start_).count());
                uint64_t v=ordinal==125?ns/100:ordinal==127?3375000ull:
                    (ns/1000000000ull)*3375000ull+(ns%1000000000ull)*3375000ull/1000000000ull;
                cpu.edx=uint32_t(v>>32);Ret(cpu,0,uint32_t(v));return;
            }
            case 151: {
                Check(cpu,1);auto us=Arg(cpu,0);
                if(us>1000000)throw std::runtime_error("Stall exceeds diagnostic budget");
                auto until=std::chrono::steady_clock::now()+std::chrono::microseconds(us);
                lock.unlock();
                while(std::chrono::steady_clock::now()<until)std::this_thread::yield();
                lock.lock();Pause(cpu,lock);Ret(cpu,1,0);return;
            }
            case 304: case 305: {
                Check(cpu,2);
                if(ordinal==304){
                    XboxTimeFields f{};uint64_t ticks=0;
                    mem_.Read(Arg(cpu,0),&f,sizeof(f));
                    if(!XboxFieldsToTicks(f,ticks)){Ret(cpu,2,0);return;}
                    mem_.Write(Arg(cpu,1),&ticks,8);Ret(cpu,2,1);
                }else{
                    uint64_t ticks=0;mem_.Read(Arg(cpu,0),&ticks,8);
                    auto f=XboxTicksToFields(ticks);mem_.Write(Arg(cpu,1),&f,sizeof(f));Ret(cpu,2,0);
                }
                return;
            }
            case 47: {
                // HAL_SHUTDOWN_REGISTRATION: callback, priority, LIST_ENTRY.
                Check(cpu,2);auto registration=Arg(cpu,0);
                if(registration%4 || !mem_.IsCommitted(registration,16))
                    throw std::runtime_error("Invalid shutdown registration");
                if(Arg(cpu,1)&255u) {
                    auto callback=mem_.Read32(registration);
                    if(!callback || !mem_.IsCommitted(callback,1))
                        throw std::runtime_error("Invalid shutdown notification callback");
                    if(shutdown_notifications_.size()>=128 && !shutdown_notifications_.count(registration))
                        throw std::runtime_error("Shutdown notification capacity exhausted");
                    shutdown_notifications_[registration]=callback;
                    std::fprintf(stderr,"Registered shutdown notification %08X callback=%08X\n",registration,callback);
                } else shutdown_notifications_.erase(registration);
                // Diagnostic process has no firmware shutdown transition yet.
                Ret(cpu,2,0);return;
            }
            case 35: Check(cpu,0);Ret(cpu,0,cache_pages_);return;
            case 37: {
                Check(cpu,1);auto pages=Arg(cpu,0);
                if(pages>16384){Ret(cpu,1,0xc000000du);return;}
                cache_pages_=pages;Ret(cpu,1,0);return;
            }
            case 24: {
                // Selected DWORD settings for the NTSC-U compatibility profile.
                Check(cpu,5);uint32_t v=0;
                switch(Arg(cpu,0)) {
                    case 7: v=1;break; // English
                    case 8: v=0x00080000;break; // 480p, 4:3
                    case 9: v=0;break; // stereo
                    case 0x12: v=1;break; // DVD region
                    case 0x103: v=0x00400100;break; // NTSC-M
                    case 0x104: v=1;break; // North America
                    default: Ret(cpu,5,0xc0000034u);return;
                }
                auto type=Arg(cpu,1),out=Arg(cpu,2),len=Arg(cpu,3),needed=Arg(cpu,4);
                for(auto p:{type,needed}) if(p&&!mem_.IsCommitted(p,4))
                    throw std::runtime_error("Invalid setting output pointer");
                Output(needed,4);
                if(len<4){Ret(cpu,5,0xc0000023u);return;}
                if(!out||!mem_.IsCommitted(out,4)){Ret(cpu,5,0xc000000du);return;}
                Output(type,4);Output(out,v);Ret(cpu,5,0);return;
            }
            case 301: {
                Check(cpu,1);uint32_t result;
                switch(Arg(cpu,0)) {
                    case 0: result=0;break;
                    case 0x102: result=1460;break;
                    case 0xc0000008u: result=6;break;
                    case 0xc000000du: result=87;break;
                    case 0xc0000011u: result=38;break;
                    case 0xc0000017u: result=8;break;
                    case 0xc0000022u: result=5;break;
                    case 0xc0000023u: result=122;break;
                    case 0xc0000034u: result=2;break;
                    case 0xc000003au: result=3;break;
                    case 0xc0000043u: result=32;break;
                    case 0xc0000185u: result=1117;break;
                    default: result=317;break;
                }
                Ret(cpu,1,result);return;
            }
            case 190: case 202: {
                unsigned n=ordinal==190?9:6;Check(cpu,n);
                auto status=files_.Open(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),
                    ordinal==190?Arg(cpu,7):1,Arg(cpu,n-1));
                Ret(cpu,n,status);return;
            }
            case 219: {
                Check(cpu,8);
                if(Arg(cpu,2)) throw std::runtime_error("File APC not implemented");
                auto event=Arg(cpu,1);
                if(event && (!events_.count(event)||semaphore_limits_.count(events_.at(event).body))){Ret(cpu,8,0xc0000008u);return;}
                auto status=files_.Read(Arg(cpu,0),Arg(cpu,4),Arg(cpu,5),Arg(cpu,6),Arg(cpu,7));
                if(event){mem_.Write32(events_.at(event).body+4,1);cv_.notify_all();}
                Ret(cpu,8,status);return;
            }
            case 211: Check(cpu,5);Ret(cpu,5,files_.Query(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4)));return;
            case 226: Check(cpu,5);Ret(cpu,5,files_.Set(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4)));return;
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
            case 165: case 166: {
                unsigned n=ordinal==165?1:5;Check(cpu,n);
                if(ordinal==166 && Arg(cpu,4)!=4)
                    throw std::runtime_error("Unsupported physical memory protection/alias");
                auto a=physical_.Allocate(Arg(cpu,0),ordinal==165?0:Arg(cpu,1),
                    ordinal==165?0xffffffffu:Arg(cpu,2),ordinal==165?0:Arg(cpu,3));
                Ret(cpu,n,a);return;
            }
            case 171: Check(cpu,1);
                if(!physical_.Free(Arg(cpu,0))) throw std::runtime_error("Invalid contiguous free");
                Ret(cpu,1,0);return;
            case 173: {
                Check(cpu,1);auto addr=Arg(cpu,0);
                if(!physical_.Size(addr)) throw std::runtime_error("No physical mapping for guest address");
                Ret(cpu,1,physical_.Physical(addr));return;
            }
            case 180: {
                Check(cpu,1);auto addr=Arg(cpu,0);
                auto size=physical_.Size(addr);
                Ret(cpu,1,size?size:virtual_.AllocationSize(addr));return;
            }
            case 335: Check(cpu,1); XbeKernelCrypto(mem_).Init(Arg(cpu,0)); Ret(cpu,1,0);return;
            case 336: Check(cpu,3); XbeKernelCrypto(mem_).Update(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2)); Ret(cpu,3,0);return;
            case 337: Check(cpu,2); XbeKernelCrypto(mem_).Final(Arg(cpu,0),Arg(cpu,1)); Ret(cpu,2,0);return;
            case 340: Check(cpu,7);
                XbeKernelCrypto(mem_).Hmac(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4),Arg(cpu,5),Arg(cpu,6));
                Ret(cpu,7,0);return;
            case 268: case 269: case 285: {
                Check(cpu,3);
                const auto dst=Arg(cpu,0);
                const auto n=ordinal==268?Arg(cpu,2):Arg(cpu,1)&~3u;
                if(n>64u*1024u*1024u || !mem_.IsCommitted(dst,n))
                    throw std::runtime_error("Invalid RTL memory range");
                uint32_t done=0;
                if(ordinal==268) {
                    auto src=Arg(cpu,1);
                    if(!mem_.IsCommitted(src,n))throw std::runtime_error("Invalid RTL compare buffer");
                    while(done<n && mem_.Base()[dst+done]==mem_.Base()[src+done])++done;
                } else if(ordinal==269) {
                    while(done<n && mem_.Read32(dst+done)==Arg(cpu,2))done+=4;
                } else {
                    for(;done<n;done+=4)mem_.Write32(dst+done,Arg(cpu,2));
                }
                Ret(cpu,3,ordinal==285?0:done);return;
            }
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
                do {
                    Pause(cpu,lock);ServiceDpc(cpu,lock);
                    if(Steady::now()>=deadline)break;
                    if(!dpc_running_&&!dpc_queue_.empty())continue;
                    cv_.wait_until(lock,NextTimer(deadline));
                }while(true);
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
            case 193: {
                Check(cpu,4);auto out=Arg(cpu,0),initial=Arg(cpu,2),limit=Arg(cpu,3);
                if(!mem_.IsCommitted(out,4)||!limit||limit>0x7fffffffu||initial>limit) {
                    Ret(cpu,4,0xc000000du);return;
                }
                if(Arg(cpu,1))throw std::runtime_error("Named semaphore not implemented");
                auto body=pool_.Allocate(20);
                if(!body){Ret(cpu,4,0xc0000017u);return;}
                auto h=next_event_;next_event_+=4;
                mem_.Write32(body,0x00050005u);mem_.Write32(body+4,initial);
                mem_.Write32(body+8,body+8);mem_.Write32(body+12,body+8);
                mem_.Write32(body+16,limit);
                events_.emplace(h,Event{body,false});semaphore_limits_.emplace(body,limit);
                Output(out,h);Ret(cpu,4,0);return;
            }
            case 222: {
                Check(cpu,3);auto h=Arg(cpu,0),add=Arg(cpu,1),out=Arg(cpu,2);
                auto it=events_.find(h);
                if(it==events_.end()||!semaphore_limits_.count(it->second.body)) {
                    Ret(cpu,3,0xc0000008u);return;
                }
                auto body=it->second.body,old=mem_.Read32(body+4);
                if(!add||uint64_t(old)+add>semaphore_limits_.at(body)) {
                    Ret(cpu,3,0xc0000047u);return;
                }
                Output(out,old);mem_.Write32(body+4,old+add);
                cv_.notify_all();Ret(cpu,3,0);return;
            }
            case 235: {
                Check(cpu,6);auto n=Arg(cpu,0),list=Arg(cpu,1),kind=Arg(cpu,2);
                if(!n||n>64||kind>1||!mem_.IsCommitted(list,4*n)) {
                    Ret(cpu,6,0xc000000du);return;
                }
                if(Arg(cpu,4)&255u)throw std::runtime_error("Alertable multiwait not implemented");
                std::vector<uint32_t> bodies;
                for(uint32_t i=0;i<n;++i) {
                    auto body=Body(mem_.Read32(list+4*i));
                    if(!body){Ret(cpu,6,0xc0000008u);return;}
                    if(std::find(bodies.begin(),bodies.end(),body)!=bodies.end()){
                        Ret(cpu,6,0xc000000du);return;
                    }
                    bodies.push_back(body);
                }
                const auto deadline=Deadline(Arg(cpu,5));
                auto ready=[&]{
                    if(stopping_)return true;
                    auto signal=[&](uint32_t b){return mem_.Read32(b+4)!=0;};
                    return kind?std::any_of(bodies.begin(),bodies.end(),signal):
                                std::all_of(bodies.begin(),bodies.end(),signal);
                };
                do {
                    Pause(cpu,lock);ServiceDpc(cpu,lock);
                    if(ready())break;
                    if(Steady::now()>=deadline){Ret(cpu,6,0x102);return;}
                    if(!dpc_running_&&!dpc_queue_.empty())continue;
                    cv_.wait_until(lock,NextTimer(deadline));
                }while(!ready());
                auto consume=[&](uint32_t body){
                    auto type=mem_.Read32(body)&255u;
                    if(type==1)mem_.Write32(body+4,0);
                    else if(type==5)mem_.Write32(body+4,mem_.Read32(body+4)-1);
                };
                if(!kind){for(auto body:bodies)consume(body);Ret(cpu,6,0);return;}
                for(uint32_t i=0;i<n;++i)if(mem_.Read32(bodies[i]+4)){
                    consume(bodies[i]);Ret(cpu,6,i);return;
                }
                throw std::runtime_error("Multiwait lost signal");
            }
            case 225: case 186: {
                const unsigned n=ordinal==225?2:1;Check(cpu,n);
                auto it=events_.find(Arg(cpu,0));
                if(it==events_.end()||semaphore_limits_.count(it->second.body)){Ret(cpu,n,0xc0000008u);return;}
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
                if(files_.Close(h)){Ret(cpu,1,0);return;}
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
                auto requested=Arg(cpu,1);
                if(requested) {
                    uint32_t ordinal_type=259;
                    if(events_.count(h)) ordinal_type=semaphore_limits_.count(body)?30:16;
                    const auto actual=registry_?registry_->Resolve(ordinal_type).guest_data:0;
                    if(!actual || actual!=requested){Ret(cpu,3,0xc0000024u);return;}
                }
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
                     XbeTlsTemplate tls={},const HulkKernelClock* clock=nullptr,const std::string& disc_root={},const HulkKernelDataRegistry* registry=nullptr)
        :mem_(mem),pool_(mem,base,size),virtual_(mem),physical_(mem),files_(mem,disc_root),tls_(tls),clock_(clock),registry_(registry) {}
    XbeKernelSession(const XbeKernelSession&)=delete;
    XbeKernelSession& operator=(const XbeKernelSession&)=delete;
    ~XbeKernelSession() { Stop(); }
    static void Dispatch(X86Context& cpu,uint8_t* base,uint32_t ordinal) {
        if(!active_ || active_->mem_.Base()!=base)
            throw std::runtime_error("Invalid kernel session");
        fault_thread=static_cast<sig_atomic_t>(cpu.thread_handle);
        fault_ordinal=static_cast<sig_atomic_t>(ordinal);
        fault_stack=static_cast<sig_atomic_t>(cpu.esp);
        fault_return=0;
        std::unique_lock<std::mutex> lock(active_->lock_);
        if(active_->mem_.IsCommitted(cpu.esp,4)) fault_return=static_cast<sig_atomic_t>(active_->mem_.Read32(cpu.esp));
        active_->Call(cpu,ordinal,lock);
        active_->ServiceDpc(cpu,lock);
    }
    int Run(Entry entry,X86Context& cpu,uint32_t address) {
        if(!entry || active_ || entry_) throw std::runtime_error("Invalid kernel session");
        struct Reset { XbeKernelSession& s; ~Reset(){s.entry_=nullptr; active_=nullptr;} } reset{*this};
        stopping_=false; worker_failure_=0;
        GuestFaultStack fault_stack_scope;
        fault_thread=static_cast<sig_atomic_t>(cpu.thread_handle);
        fault_ordinal=0;fault_return=0;fault_stack=static_cast<sig_atomic_t>(cpu.esp);
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
