#pragma once
#include "xbe_kernel_pool_calls.h"
#include "xbe_kernel_thread.h"
#include "xbe_tls.h"
#include "hulk_kernel_clock.h"
#include "xbe_guest_thread_exit.h"
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
        bool finished=false;
    };
    XboxGuestAddressSpace& mem_;
    XbeKernelPool pool_;
    XbeTlsTemplate tls_;
    const HulkKernelClock* clock_;
    std::mutex lock_;
    std::condition_variable cv_;
    std::array<Worker,16> workers_{};
    size_t count_=0;
    bool stopping_=false;
    int worker_failure_=0;
    Entry entry_=nullptr;
    inline static thread_local XbeKernelSession* active_=nullptr;
    Worker* Find(uint32_t handle) {
        for(size_t i=0;i<count_;++i)
            if(workers_[i].handle==handle) return &workers_[i];
        return nullptr;
    }
    void Pause(X86Context& cpu,std::unique_lock<std::mutex>& lock) {
        auto* w=Find(cpu.thread_handle);
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
    void Call(X86Context& cpu,uint32_t ordinal,std::unique_lock<std::mutex>& lock) {
        Pause(cpu,lock);
        if(clock_) clock_->Update(mem_);
        switch(ordinal) {
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
        :mem_(mem),pool_(mem,base,size),tls_(tls),clock_(clock) {}
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
