#include <jni.h>
#include <android/native_window_jni.h>
#include <unistd.h>
#include <algorithm>
#include <deque>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <vector>
#define main HulkPreviewMain
#include "hulk_xbe_local_diagnostic.cpp"
#undef main
namespace {
std::atomic<bool> running{false};
std::string Str(JNIEnv* e,jstring s){
    if(!s)return {};
    const char* p=e->GetStringUTFChars(s,nullptr);
    if(!p)return {};
    std::string r(p);e->ReleaseStringUTFChars(s,p);return r;
}
class Report {
    int saved_=-1,saved_out_=-1;
    std::thread drain_;
public:
    explicit Report(const std::string& path){
        int p[2];if(pipe(p))throw std::runtime_error("Preview report pipe failed");
        saved_=dup(STDERR_FILENO);saved_out_=dup(STDOUT_FILENO);
        if(saved_<0||saved_out_<0||dup2(p[1],STDERR_FILENO)<0||dup2(p[1],STDOUT_FILENO)<0){
            if(saved_>=0){dup2(saved_,STDERR_FILENO);close(saved_);}
            if(saved_out_>=0){dup2(saved_out_,STDOUT_FILENO);close(saved_out_);}
            close(p[0]);close(p[1]);throw std::runtime_error("Preview report redirect failed");
        }
        close(p[1]);
        drain_=std::thread([fd=p[0],path]{
            std::deque<std::string> tail,events,input_events;
            std::string last_swap,last_checksum;
            std::string line;char buffer[8192];uint64_t n=0;
            auto last=std::chrono::steady_clock::now();
            auto flush=[&]{
                std::ofstream out(path+".tmp",std::ios::trunc);if(!out)return;
                out<<"HulkPocket preview report lines="<<n<<"\n";
                out<<"Retained input events:\n";
                for(const auto& l:input_events)out<<l<<'\n';
                out<<"Retained graphics and error events:\n";
                for(const auto& l:events)out<<l<<'\n';
                out<<last_swap<<'\n'<<last_checksum<<"\nFinal raw context:\n";
                for(const auto& l:tail)out<<l<<'\n';
                out.close();if(out)std::rename((path+".tmp").c_str(),path.c_str());
            };
            for(;;){
                auto count=read(fd,buffer,sizeof(buffer));if(count<0&&errno==EINTR)continue;
                if(count<=0)break;
                for(ssize_t i=0;i<count;++i){
                    if(buffer[i]=='\n'){
                        ++n;
                        if(line.starts_with("HULK INPUT ")){
                            input_events.push_back(line);if(input_events.size()>256)input_events.pop_front();
                        }
                        if(line.starts_with("Guest stopped:")||
                           line.starts_with("HULK AAUDIO SUMMARY")||line.starts_with("HULK AUDIO SUMMARY")||
                           (line.starts_with("D3D LIGHT ")&&(!line.starts_with("D3D LIGHT OBSERVE")||line.ends_with("accepted=0")))||
                           line.starts_with("D3D PACKED NORMAL ")||line.starts_with("D3D INVALID POSITION")||
                           line.starts_with("D3D XYZ ISOLATION")||line.starts_with("Guest entry status:")){
                            events.push_back(line);if(events.size()>256)events.pop_front();
                        }
                        if(line.starts_with("D3D ORIGINAL SWAP OFFSCREEN PASS:"))last_swap=line;
                        if(line.starts_with("D3D FRAME CHECKSUM "))last_checksum=line;
                        tail.push_back(std::move(line));line.clear();if(tail.size()>4096)tail.pop_front();
                    }
                    else if(line.size()<4096)line+=buffer[i];
                }
                auto now=std::chrono::steady_clock::now();
                if(now-last>std::chrono::seconds(2)){flush();last=now;}
            }
            if(!line.empty()){tail.push_back(line);if(tail.size()>4096)tail.pop_front();}
            flush();close(fd);
        });
    }
    ~Report(){
        std::cout.flush();std::cerr.flush();std::fflush(nullptr);
        dup2(saved_,STDERR_FILENO);dup2(saved_out_,STDOUT_FILENO);
        close(saved_);close(saved_out_);if(drain_.joinable())drain_.join();
    }
};
}
extern "C" JNIEXPORT void JNICALL Java_com_hulkpocket_diagnostic_GameActivity_nativeSurface(JNIEnv* e,jclass,jobject surface){
    ANativeWindow* w=surface?ANativeWindow_fromSurface(e,surface):nullptr;
    hulkpocket::preview_host.Window(w);if(w)ANativeWindow_release(w);
}
extern "C" JNIEXPORT void JNICALL Java_com_hulkpocket_diagnostic_GameActivity_nativeInput(JNIEnv* e,jclass,jboolean connected,jint buttons,jbyteArray analog,jint lx,jint ly,jint rx,jint ry){
    hulkpocket::HulkPadScript::State s{};s[0]=uint8_t(buttons);s[1]=uint8_t(buttons>>8);
    if(analog&&e->GetArrayLength(analog)==8)e->GetByteArrayRegion(analog,0,8,reinterpret_cast<jbyte*>(s.data()+2));
    unsigned i=10;for(int v:{lx,ly,rx,ry}){uint16_t n=uint16_t(std::clamp(v,-32768,32767));s[i++]=uint8_t(n);s[i++]=uint8_t(n>>8);}
    hulkpocket::preview_host.Input(connected,s);
}
extern "C" JNIEXPORT void JNICALL Java_com_hulkpocket_diagnostic_GameActivity_nativeStop(JNIEnv*,jclass){hulkpocket::preview_host.stop.store(true);}
extern "C" JNIEXPORT jint JNICALL Java_com_hulkpocket_diagnostic_GameActivity_nativeRun(JNIEnv* e,jclass,jstring disc,jstring module,jstring partition,jstring log,jstring fvf){
    if(running.exchange(true))return 2;
    int result=2;
    try{
        auto d=Str(e,disc),m=Str(e,module),p=Str(e,partition),l=Str(e,log);
        Report report(l);
        auto f=Str(e,fvf);
        if(f.size()!=8||f.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)
            throw std::runtime_error("Invalid FVF in local preview package");
        setenv("HULK_FVF_SHADER",f.c_str(),1);
        setenv("HULK_ISOLATE_INVALID_XYZ","1",1);
        setenv("HULK_CONTINUE_CREATE","1",1);setenv("HULK_CONTINUE_DRAW","1",1);setenv("HULK_CONTINUE_SWAP","1",1);
        setenv("HULK_FRAME_LIMIT","10800",1);setenv("HULK_WATCHDOG_SECONDS","600",1);setenv("HULK_DRAW_LIMIT","128",1);
        setenv("HULK_COMPACT_AUDIO_TRACE","1",1);setenv("HULK_COMPACT_REPORT","1",1);setenv("HULK_AUDIO_OUTPUT","1",1);
        unsetenv("HULK_INPUT_SCRIPT");unsetenv("HULK_AUDIO_PCM");unsetenv("HULK_REPLAY_BITMAP");
        hulkpocket::preview_host.enabled.store(true);
        std::vector<std::string> args={"hulk-preview",d+"/default.xbe",m,"--execute-diagnostic","--disc-root",d,"--partition1-root",p};
        std::vector<char*> argv;for(auto& s:args)argv.push_back(s.data());argv.push_back(nullptr);
        result=HulkPreviewMain(int(args.size()),argv.data());
    }catch(const std::exception&){result=5;}
    alarm(0);hulkpocket::preview_host.enabled.store(false);running.store(false);return result;
}
