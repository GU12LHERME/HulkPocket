#pragma once
// Native diagnostic executable has no SDL activity. Use the same bounded
// producer/consumer separation as the Android SCDA output architecture,
// but load Android's AAudio dynamically so API 24 remains linkable.
#if defined(__ANDROID__) && defined(HULK_ENABLE_GLES_DEVICE_PROBE) && __has_include(<aaudio/AAudio.h>)
#define HULK_HAS_AAUDIO_OUTPUT 1
#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <cstdio>
#include "hulk_audio_queue.h"

namespace hulkpocket {
class HulkAndroidAudio {
    void* library_=nullptr;
    AAudioStream* stream_=nullptr;
    bool attempted_=false, started_=false;
    std::atomic<int32_t> error_{0};
    aaudio_result_t (*close_)(AAudioStream*)=nullptr;
    aaudio_result_t (*stop_)(AAudioStream*)=nullptr;
    template<class T> bool Load(T& fn,const char* name){
        fn=reinterpret_cast<T>(dlsym(library_,name));return fn!=nullptr;
    }
    static aaudio_data_callback_result_t Feed(AAudioStream*,void* self,void* data,int32_t frames){
        if(!data||frames<=0)return AAUDIO_CALLBACK_RESULT_STOP;
        static_cast<HulkAndroidAudio*>(self)->queue.Pull(static_cast<int16_t*>(data),std::size_t(frames));
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    static void Error(AAudioStream*,void* self,aaudio_result_t error){
        static_cast<HulkAndroidAudio*>(self)->error_.store(error,std::memory_order_relaxed);
    }
public:
    HulkAudioQueue<> queue;
    HulkAndroidAudio()=default;
    HulkAndroidAudio(const HulkAndroidAudio&)=delete;
    HulkAndroidAudio& operator=(const HulkAndroidAudio&)=delete;
    ~HulkAndroidAudio(){
        if(stream_){
            if(started_)stop_(stream_);
            close_(stream_);
        }
        if(attempted_)std::fprintf(stderr,"HULK AAUDIO SUMMARY started=%u underrun_frames=%u overrun_frames=%u error=%d\n",
            unsigned(started_),queue.Underrun(),queue.Overrun(),int(error_.load()));
        if(library_)dlclose(library_);
    }
    bool Started()const{return started_;}
    bool Attempted()const{return attempted_;}
    bool Start(){
        if(attempted_)return started_;
        attempted_=true;
        library_=dlopen("libaaudio.so",RTLD_NOW|RTLD_LOCAL);
        if(!library_){std::fprintf(stderr,"HULK AAUDIO unavailable; retaining PCM capture\n");return false;}
        aaudio_result_t (*create)(AAudioStreamBuilder**)=nullptr;
        aaudio_result_t (*destroy)(AAudioStreamBuilder*)=nullptr;
        void (*direction)(AAudioStreamBuilder*,aaudio_direction_t)=nullptr;
        void (*format)(AAudioStreamBuilder*,aaudio_format_t)=nullptr;
        void (*channels)(AAudioStreamBuilder*,int32_t)=nullptr;
        void (*rate)(AAudioStreamBuilder*,int32_t)=nullptr;
        void (*callback)(AAudioStreamBuilder*,AAudioStream_dataCallback,void*)=nullptr;
        void (*errorcb)(AAudioStreamBuilder*,AAudioStream_errorCallback,void*)=nullptr;
        aaudio_result_t (*open)(AAudioStreamBuilder*,AAudioStream**)=nullptr;
        aaudio_result_t (*start)(AAudioStream*)=nullptr;
        int32_t (*getrate)(AAudioStream*)=nullptr;
        int32_t (*getchannels)(AAudioStream*)=nullptr;
        aaudio_format_t (*getformat)(AAudioStream*)=nullptr;
        if(!Load(create,"AAudio_createStreamBuilder")||!Load(destroy,"AAudioStreamBuilder_delete")||
           !Load(direction,"AAudioStreamBuilder_setDirection")||!Load(format,"AAudioStreamBuilder_setFormat")||
           !Load(channels,"AAudioStreamBuilder_setChannelCount")||!Load(rate,"AAudioStreamBuilder_setSampleRate")||
           !Load(callback,"AAudioStreamBuilder_setDataCallback")||!Load(errorcb,"AAudioStreamBuilder_setErrorCallback")||
           !Load(open,"AAudioStreamBuilder_openStream")||!Load(start,"AAudioStream_requestStart")||
           !Load(stop_,"AAudioStream_requestStop")||!Load(close_,"AAudioStream_close")||
           !Load(getrate,"AAudioStream_getSampleRate")||!Load(getchannels,"AAudioStream_getChannelCount")||
           !Load(getformat,"AAudioStream_getFormat")){
            std::fprintf(stderr,"HULK AAUDIO API unavailable; retaining PCM capture\n");return false;
        }
        AAudioStreamBuilder* b=nullptr;
        auto result=create(&b);
        if(result!=AAUDIO_OK||!b){std::fprintf(stderr,"HULK AAUDIO builder failed=%d\n",int(result));return false;}
        direction(b,AAUDIO_DIRECTION_OUTPUT);format(b,AAUDIO_FORMAT_PCM_I16);channels(b,2);rate(b,48000);
        callback(b,Feed,this);errorcb(b,Error,this);
        result=open(b,&stream_);destroy(b);
        if(result!=AAUDIO_OK||!stream_){std::fprintf(stderr,"HULK AAUDIO open failed=%d\n",int(result));return false;}
        if(getrate(stream_)!=48000||getchannels(stream_)!=2||getformat(stream_)!=AAUDIO_FORMAT_PCM_I16){
            std::fprintf(stderr,"HULK AAUDIO incompatible negotiated format; retaining PCM capture\n");return false;
        }
        result=start(stream_);
        if(result!=AAUDIO_OK){std::fprintf(stderr,"HULK AAUDIO start failed=%d\n",int(result));return false;}
        started_=true;
        std::fprintf(stderr,"HULK AAUDIO START requested: 48000 Hz stereo PCM16, 30ms prebuffer\n");
        return true;
    }
};
}
#endif
