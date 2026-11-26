#pragma once
#include "xbe_kernel_pool_calls.h"
#include "xbe_kernel_thread.h"
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
namespace hulkpocket {
class XbeKernelSession {
    XboxGuestAddressSpace& mem_;
    XbeKernelPool pool_;
    std::mutex lock_;
    std::vector<std::thread> workers_;
    uint32_t next_thread_=0;
    int worker_failure_=0;
    using EntryFn=int(*)(X86Context*,uint8_t*,uint32_t,void(*)(X86Context&,uint8_t*,uint32_t));
    EntryFn entry_=nullptr;
    inline static thread_local XbeKernelSession* active_=nullptr;
public:
    XbeKernelSession(XboxGuestAddressSpace& mem,uint32_t base,uint32_t size)
        :mem_(mem),pool_(mem,base,size) {}
    XbeKernelSession(const XbeKernelSession&)=delete;
    XbeKernelSession& operator=(const XbeKernelSession&)=delete;
    ~XbeKernelSession() { for(auto& t:workers_) if(t.joinable()) t.join(); }
    using Callback=void(*)(X86Context&,uint8_t*,uint32_t);
    using Entry=int(*)(X86Context*,uint8_t*,uint32_t,Callback);
    static void Dispatch(X86Context& cpu,uint8_t* base,uint32_t ordinal) {
        if(!active_ || active_->mem_.Base()!=base)
            throw std::runtime_error("Invalid kernel session");
        std::lock_guard<std::mutex> guard(active_->lock_);
        if(ordinal==255) {
            active_->CreateThread(cpu);
            return;
        }
        if(!DispatchPoolCall(ordinal,cpu,active_->mem_,active_->pool_))
            throw std::runtime_error("Unsupported kernel ordinal "+std::to_string(ordinal));
    }
    void CreateThread(X86Context& cpu) {
        if(!entry_ || !mem_.IsCommitted(cpu.esp,44))
            throw std::runtime_error("Invalid thread creation stack");
        uint32_t a[10];
        for(unsigned i=0;i<10;++i) a[i]=mem_.Read32(cpu.esp+4+i*4);
        if((a[7]&255u) || a[3] || a[2]>0x80000 || !a[5] || next_thread_>=16)
            throw std::runtime_error("Unsupported thread creation parameters (suspend/TLS/stack)");
        for(auto out:{a[0],a[4]})
            if(out && !mem_.IsCommitted(out,4)) throw std::runtime_error("Invalid thread output");
        uint32_t slot=0x0A000000u+next_thread_*0x100000u;
        uint32_t handle=0x100u+next_thread_*4u;
        if(mem_.IsCommitted(slot,mem_.PageSize())) throw std::runtime_error("Thread arena occupied");
        mem_.Commit(slot,0x1000);
        uint32_t stack=slot+0x10000, top=slot+0x90000;
        mem_.Commit(stack,top-stack);
        X86Context child{};
        child.esp=top-16;
        child.fs_base=slot;
        child.thread_handle=handle;
        child.fcw=0x027f;
        child.mxcsr=0x1f80;
        mem_.Write32(child.esp,0);
        mem_.Write32(child.esp+4,a[9]?a[5]:a[6]);
        if(a[9]) mem_.Write32(child.esp+8,a[6]);
        InitializeKernelThread(mem_,slot,slot+0x400,top,stack,child.esp,0,handle);
        if(a[0]) mem_.Write32(a[0],handle);
        if(a[4]) mem_.Write32(a[4],handle);
        ++next_thread_;
        auto target=a[9]?a[9]:a[5];
        workers_.emplace_back([this,child,target]() mutable {
            active_=this;
            int result=entry_(&child,mem_.Base(),target,Dispatch);
            active_=nullptr;
            std::lock_guard<std::mutex> guard(lock_);
            if(result) worker_failure_=result;
        });
        std::fprintf(stderr,"Created diagnostic guest thread %X at %08X\\n",handle,target);
        cpu.eax=0;
        cpu.esp+=44;
    }
    int Run(Entry entry,X86Context& cpu,uint32_t address) {
        if(!entry || active_) throw std::runtime_error("Invalid nested kernel session");
        struct Reset { ~Reset(){active_=nullptr;} } reset;
        active_=this;
        entry_=entry;
        int result=entry(&cpu,mem_.Base(),address,Dispatch);
        // Workers may create more workers; take each handle under the lock.
        for(size_t i=0;;++i) {
            std::thread worker;
            { std::lock_guard<std::mutex> guard(lock_);
              if(i>=workers_.size()) break;
              worker=std::move(workers_[i]); }
            if(worker.joinable()) worker.join();
        }
        return result?result:worker_failure_;
    }
};
}
