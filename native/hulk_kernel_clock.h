#pragma once
#include "hulk_kernel_data_registry.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <signal.h>
#include <pthread.h>
namespace hulkpocket {
// Xbox exports 156 (volatile DWORD) and 157 (const ULONG).
// Interval is supplied by the runtime in units of 100 ns.
class HulkKernelClock {
    using Clock=std::chrono::steady_clock;
    Clock::time_point start_;
    uint32_t tick_, increment_, interval_;
public:
    HulkKernelClock(XboxGuestAddressSpace& mem,HulkKernelDataRegistry& registry,
                    uint32_t address,uint32_t interval)
        :start_(Clock::now()),tick_(address),increment_(address+4),interval_(interval) {
        if(!interval || address>0xfffffff8u || address%4 ||
           !mem.IsCommitted(address,8))
            throw std::invalid_argument("Invalid kernel clock storage");
        auto staged=registry;
        staged.Register(156,tick_,4,mem);
        staged.Register(157,increment_,4,mem);
        mem.Write32(tick_,0);
        mem.Write32(increment_,interval_);
        registry=staged;
    }
    void Update(XboxGuestAddressSpace& mem) const {
        auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start_).count();
        // Stable, aligned guest DWORD. Use a single atomic host store,
        // rather than copying bytes while another guest thread polls it.
        __atomic_store_n(reinterpret_cast<uint32_t*>(mem.Base()+tick_),
            static_cast<uint32_t>(ns/100/interval_),__ATOMIC_RELAXED);
    }
};
// Only start after fork, and join before releasing guest memory. The
// main/guest threads do not also update the tick while this pump is active.
class HulkKernelClockPump {
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_=false;
    std::thread worker_;
public:
    HulkKernelClockPump(XboxGuestAddressSpace& mem,const HulkKernelClock* clock){
        if(!clock)return;
        clock->Update(mem);
        worker_=std::thread([this,&mem,clock]{
            sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGALRM);
            pthread_sigmask(SIG_BLOCK,&mask,nullptr);
            std::unique_lock<std::mutex> lock(mutex_);
            while(!stopped_){
                clock->Update(mem);
                cv_.wait_for(lock,std::chrono::milliseconds(1),[this]{return stopped_;});
            }
        });
    }
    ~HulkKernelClockPump(){
        {std::lock_guard<std::mutex> lock(mutex_);stopped_=true;}
        cv_.notify_all();
        if(worker_.joinable())worker_.join();
    }
    HulkKernelClockPump(const HulkKernelClockPump&)=delete;
    HulkKernelClockPump& operator=(const HulkKernelClockPump&)=delete;
};
}
