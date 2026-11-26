#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstddef>

namespace hulkpocket {
// Single producer (guest mixer), single consumer (host audio callback).
// No guest pointers, heap allocations, or locks on the audio thread.
// Full queue drops newest frames; empty queue supplies silence and counts it.
template<std::size_t N=8192> class HulkAudioQueue {
    static_assert(N>0 && (N&(N-1))==0 && N<(1u<<30),"power-of-two capacity");
    std::array<int16_t,N*2> data_{};
    std::atomic<uint32_t> write_{0},read_{0},overrun_{0},underrun_{0};
public:
    std::size_t Push(const int16_t* src,std::size_t frames){
        auto w=write_.load(std::memory_order_relaxed);
        auto r=read_.load(std::memory_order_acquire);
        auto n=std::min(frames,N-std::size_t(w-r));
        for(std::size_t i=0;i<n;++i){
            auto slot=(w+uint32_t(i))&(N-1);
            data_[slot*2]=src[i*2];data_[slot*2+1]=src[i*2+1];
        }
        write_.store(w+uint32_t(n),std::memory_order_release);
        overrun_.fetch_add(uint32_t(frames-n),std::memory_order_relaxed);
        return n;
    }
    std::size_t Pull(int16_t* dst,std::size_t frames){
        auto r=read_.load(std::memory_order_relaxed);
        auto w=write_.load(std::memory_order_acquire);
        auto n=std::min(frames,std::size_t(w-r));
        for(std::size_t i=0;i<n;++i){
            auto slot=(r+uint32_t(i))&(N-1);
            dst[i*2]=data_[slot*2];dst[i*2+1]=data_[slot*2+1];
        }
        std::fill(dst+n*2,dst+frames*2,int16_t(0));
        read_.store(r+uint32_t(n),std::memory_order_release);
        underrun_.fetch_add(uint32_t(frames-n),std::memory_order_relaxed);
        return n;
    }
    // Producer-side snapshot; the consumer may have drained more since load.
    std::size_t Queued()const{
        auto w=write_.load(std::memory_order_relaxed);
        auto r=read_.load(std::memory_order_acquire);
        return std::size_t(w-r);
    }
    uint32_t Overrun()const{return overrun_.load(std::memory_order_relaxed);}
    uint32_t Underrun()const{return underrun_.load(std::memory_order_relaxed);}
};
}
