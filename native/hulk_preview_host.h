#pragma once
#include "hulk_pad_script.h"
#include <atomic>
#include <mutex>
#include <cstring>
#ifdef __ANDROID__
#include <android/native_window.h>
#endif
namespace hulkpocket {
// Session bridge for the isolated Android Activity process. Headless builds
// leave it disabled; no dependency on the app's Java thread in the guest.
class HulkPreviewHost {
    std::mutex mutex_;
    HulkPadScript::State pad_{};
    bool connected_=false;
#ifdef __ANDROID__
    ANativeWindow* window_=nullptr;
#endif
public:
    std::atomic<bool> enabled{false},stop{false};
    void Input(bool connected,const HulkPadScript::State& state){
        std::lock_guard lock(mutex_);connected_=connected;pad_=connected?state:HulkPadScript::State{};
    }
    std::pair<bool,HulkPadScript::State> Poll(){
        std::lock_guard lock(mutex_);return {connected_,pad_};
    }
#ifdef __ANDROID__
    // Takes its own reference; caller retains ownership of the supplied ref.
    void Window(ANativeWindow* w){
        std::lock_guard lock(mutex_);
        if(w)ANativeWindow_acquire(w);
        if(window_)ANativeWindow_release(window_);
        window_=w;
    }
    void Present(const uint8_t* bgra,unsigned w,unsigned h,unsigned pitch){
        std::lock_guard lock(mutex_);
        if(!window_||!bgra||!w||!h||w>4096||h>4096||pitch<uint64_t(w)*4)return;
        if(ANativeWindow_setBuffersGeometry(window_,int(w),int(h),WINDOW_FORMAT_RGBA_8888))return;
        ANativeWindow_Buffer b{};
        if(ANativeWindow_lock(window_,&b,nullptr))return;
        if(b.bits&&b.width==int(w)&&b.height==int(h)&&b.stride>=int(w)&&b.format==WINDOW_FORMAT_RGBA_8888){
            auto* dst=static_cast<uint8_t*>(b.bits);
            for(unsigned y=0;y<h;++y){
                const auto* src=bgra+size_t(y)*pitch;
                auto* row=dst+size_t(y)*b.stride*4;
                for(unsigned x=0;x<w;++x){row[x*4]=src[x*4+2];row[x*4+1]=src[x*4+1];row[x*4+2]=src[x*4];row[x*4+3]=255;}
            }
        }
        ANativeWindow_unlockAndPost(window_);
    }
#else
    void Present(const uint8_t*,unsigned,unsigned,unsigned){}
#endif
};
inline HulkPreviewHost preview_host;
}
