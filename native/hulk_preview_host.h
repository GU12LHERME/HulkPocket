#pragma once
#include "hulk_pad_script.h"
#include "hulk_frame_present.h"
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
    // Geometry last applied to window_; reset whenever the window changes so
    // setBuffersGeometry runs only on a new surface or a size change.
    unsigned geometry_w_=0,geometry_h_=0;
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
        geometry_w_=geometry_h_=0;
    }
    void Present(const uint8_t* bgra,unsigned w,unsigned h,unsigned pitch){
        if(!bgra||!w||!h||w>4096||h>4096||pitch<uint64_t(w)*4)return;
        ANativeWindow* target=nullptr;
        bool set_geometry=false;
        {
            std::lock_guard lock(mutex_);target=window_;
            if(target){
                ANativeWindow_acquire(target);
                set_geometry=geometry_w_!=w||geometry_h_!=h;
            }
        }
        if(!target)return;
        struct Ref {ANativeWindow* w;~Ref(){ANativeWindow_release(w);}} ref{target};
        if(set_geometry){
            if(ANativeWindow_setBuffersGeometry(target,int(w),int(h),WINDOW_FORMAT_RGBA_8888))return;
            std::lock_guard lock(mutex_);
            if(window_==target){geometry_w_=w;geometry_h_=h;}
        }
        ANativeWindow_Buffer b{};
        if(ANativeWindow_lock(target,&b,nullptr))return;
        if(b.bits&&b.width==int(w)&&b.height==int(h)&&b.stride>=int(w)&&b.format==WINDOW_FORMAT_RGBA_8888){
            auto* dst=static_cast<uint8_t*>(b.bits);
            for(unsigned y=0;y<h;++y)
                SwizzleBgraToOpaqueRgba(bgra+size_t(y)*pitch,dst+size_t(y)*b.stride*4,w);
        }else{
            // Surface geometry no longer matches (e.g. recreated externally):
            // reapply it on the next frame instead of dropping frames forever.
            std::lock_guard lock(mutex_);
            if(window_==target)geometry_w_=geometry_h_=0;
        }
        ANativeWindow_unlockAndPost(target);
    }
#else
    void Present(const uint8_t*,unsigned,unsigned,unsigned){}
#endif
};
inline HulkPreviewHost preview_host;
}
