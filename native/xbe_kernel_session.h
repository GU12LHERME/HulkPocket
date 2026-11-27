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
#include "xbe_kernel_stacks.h"
#include "xbe_kernel_files.h"
#include "xbe_fault_diagnostic.h"
#include "xbe_graphics_boundary.h"
#include "hulk_sound.h"
#include "hulk_pad_script.h"
#include "hulk_preview_host.h"
#include "xbe_resident_sections.h"
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
#include "xbe_d3d8_create_probe.h"
#include "xbe_preview_backend.h"
#include "xbe_d3d8_default_trace.h"
#include "xbe_diagnostic_bitmap.h"
#endif
#include "xbe_kernel_calendar.h"
#include <map>
#include <deque>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <memory>
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
        uint32_t suspend=0, pcr=0, handle=0, id=0, stack_pages=0;
        bool finished=false, closed=false;
    };
    XboxGuestAddressSpace& mem_;
    XbeKernelPool pool_;
    HulkSound sound_;
    XbeVirtualMemory virtual_;
    XbePhysicalMemory physical_;
    XbeKernelStacks stacks_;
    XbeDiscFiles files_;
    XbeTlsTemplate tls_;
    const HulkKernelClock* clock_;
    const HulkKernelDataRegistry* registry_;
    XbeResidentSections* sections_;
    std::mutex lock_;
    std::condition_variable cv_;
    std::array<Worker,16> workers_{};
    size_t count_=0;
    struct ThreadSettings {int32_t priority=0;bool disable_boost=false;};
    std::map<uint32_t,ThreadSettings> thread_settings_;
    std::map<uint32_t,uint32_t> object_references_;
    uint32_t gamepad_type_=0,memory_unit_type_=0;
    struct InputHandle {uint32_t port=0,packet=0;HulkPadScript::State last{};bool polled=false;};
    std::map<uint32_t,InputHandle> input_handles_;
    HulkPadScript pad_script_;
    unsigned input_frame_=0;
    void RefreshPreviewPorts(bool connected){
        if(!gamepad_type_||!preview_host.enabled.load())return;
        const uint32_t current=connected?1u:0u;
        const auto old=mem_.Read32(gamepad_type_);
        if(old!=current){
            mem_.Write32(gamepad_type_,current);
            mem_.Write32(gamepad_type_+4,mem_.Read32(gamepad_type_+4)|(old^current));
        }
    }
    void RefreshPreviewPorts(){
        if(gamepad_type_&&preview_host.enabled.load())
            RefreshPreviewPorts(preview_host.Poll().first);
    }
    // Objects remain owned by the session until shutdown, including bodies
    // held by in-flight waits. References are tracked independently of handles.
    struct Event { uint32_t body; bool manual; };
    std::map<uint32_t,uint32_t> semaphore_limits_;
    struct Mutant {uint32_t owner=0,depth=0;bool abandoned=false;};
    std::map<uint32_t,Mutant> mutants_;
    std::map<uint32_t,Event> events_;
    uint32_t next_event_=0x2000;
    uint32_t next_completion_=0x60000000;
    std::map<uint32_t,std::shared_ptr<XbeIoCompletion>> completions_;
    std::map<uint32_t,uint32_t> critical_;
    std::map<uint32_t,uint32_t> shutdown_notifications_;
    struct Timer {std::chrono::steady_clock::time_point due;uint32_t dpc;uint32_t period;};
    std::map<uint32_t,Timer> timers_;
    std::map<uint32_t,uint32_t> timer_types_;
    std::map<uint32_t,uint32_t> pointer_events_;
    std::map<uint32_t,uint32_t> dpc_routines_;
    std::deque<uint32_t> dpc_queue_;
    bool dpc_running_=false;
    bool sound_callback_running_=false;
    std::map<uint32_t,uint8_t> irql_;
    uint8_t& Irql(const X86Context& cpu){return irql_[cpu.thread_handle];}
    std::map<uint32_t,uint32_t> system_allocations_;
    struct KernelTrace {
        uint64_t seq=0;uint32_t ordinal=0,thread=0,ret=0,esp=0,eax=0;
        bool returned=false;
    };
    std::array<KernelTrace,48> recent_{};
    uint64_t trace_seq_=0;
    unsigned startup_text_lines_=0;
    uint32_t cache_pages_=16;
    uint32_t image_pages_=0, initial_stack_pages_=0;
    const std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
    bool stopping_=false;
    int worker_failure_=0;
    Entry entry_=nullptr;
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
    // EGL objects must be destroyed on the guest host thread that created them.
    struct LiveD3dDevice {
        XbeD3d8DeviceMemory buffers;
        XbePreviewBackend backend;
        XbeD3d8Bridge bridge;
        uint32_t owner;
        unsigned setter_calls=0,diagnostic_draw_returns=0,diagnostic_gpu_waits=0,diagnostic_vblanks=0;
        unsigned setters_at_swap=0,waits_at_swap=0,vblanks_at_swap=0;
        uint32_t diagnostic_vertices=0;
        const bool expanded_preview=VulkanPreviewSelected()&&preview_host.enabled.load();
        unsigned diagnostic_swap_returns=0,draws_at_swap=0;
        std::unique_ptr<gpu::State> diagnostic_line_profile,diagnostic_texture_profile;
        unsigned diagnostic_texture_returns=0,diagnostic_lit_returns=0;
        uint64_t last_frame_hash=0;
        const bool compact_capture=std::getenv("HULK_COMPACT_REPORT")&&
            std::strcmp(std::getenv("HULK_COMPACT_REPORT"),"1")==0;
        std::vector<uint8_t> last_presented_pixels;
        unsigned last_presented_width=0,last_presented_height=0,last_presented_pitch=0;
        void SaveLastPresented()const{
            if(last_presented_pixels.empty())return;
            if(const char* path=std::getenv("HULK_REPLAY_BITMAP");path&&*path){
                try{WriteDiagnosticBitmap(path,last_presented_pixels.data(),last_presented_width,last_presented_height,last_presented_pitch);}
                catch(const std::exception& ex){std::fprintf(stderr,"D3D SWAP BITMAP FAILED: %s\n",ex.what());}
            }
        }
        unsigned identical_frames=0,clear_calls=0,viewport_trace_enters=0;
        bool viewport_trace_sampled=false;
        const unsigned diagnostic_draw_limit=expanded_preview
            ?DiagnosticBudget("HULK_DRAW_LIMIT",kVulkanPreviewDrawsPerFrame,kVulkanPreviewMaxDrawsPerFrame)
            :DiagnosticBudget("HULK_DRAW_LIMIT",kDiagnosticDrawsPerFrame,512);
        bool TraceGraphics()const{
            return !expanded_preview||!compact_capture||detailed_draws||
                diagnostic_swap_returns<2||DiagnosticTracePoll(diagnostic_draw_returns);
        }
        bool SetterLimitReached()const{
            return expanded_preview?PreviewBudgetReached(setter_calls,setters_at_swap,kVulkanPreviewSettersPerFrame)
                :setter_calls>=DiagnosticSetterLimit(frame_limit);
        }
        bool WaitLimitReached(bool vblank)const{
            auto total=vblank?diagnostic_vblanks:diagnostic_gpu_waits;
            auto base=vblank?vblanks_at_swap:waits_at_swap;
            return expanded_preview?PreviewBudgetReached(total,base,kVulkanPreviewWaitsPerFrame):total>=4*frame_limit;
        }
        const unsigned frame_limit=DiagnosticFrameLimit();
        const bool detailed_draws=std::getenv("HULK_FULL_DRAW_CAPTURE")&&
            std::strcmp(std::getenv("HULK_FULL_DRAW_CAPTURE"),"1")==0;
        uint32_t last_capture_format=0xffffffffu,last_capture_primitive=0xffffffffu;
        std::array<uint32_t,6> last_viewport{};
        uint32_t last_viewport_call=0,last_viewport_caller=0;
        LiveD3dDevice(XboxGuestAddressSpace& mem,XbeKernelPool& pool,XbePhysicalMemory& physical,uint32_t thread)
            :buffers(mem,pool,physical),bridge(mem,backend,VulkanPreviewSelected()),owner(thread){}
    };
    std::unique_ptr<LiveD3dDevice> live_d3d_;
    void TraceViewport(LiveD3dDevice& live,uint32_t ordinal,uint32_t caller){
        std::array<uint32_t,6> now{};
        for(unsigned i=0;i<now.size();++i)now[i]=mem_.Read32(live.buffers.DeviceAddress()+0xee0+4*i);
        if(now!=live.last_viewport){
            auto device=live.buffers.DeviceAddress();
            auto target=mem_.Read32(device+0x1a04);
            if(target&&mem_.IsCommitted(target,20))
                std::fprintf(stderr,"D3D VIEWPORT TARGET header=%08X common=%08X data=%08X format=%08X size=%08X\n",
                    target,mem_.Read32(target),mem_.Read32(target+4),mem_.Read32(target+12),mem_.Read32(target+16));
            std::fprintf(stderr,"D3D VIEWPORT CHANGE previous_id=%08X previous_caller=%08X current_id=%08X current_caller=%08X old=%u,%u,%u,%u new=%u,%u,%u,%u z=%08X,%08X\n",
                live.last_viewport_call,live.last_viewport_caller,ordinal,caller,
                live.last_viewport[0],live.last_viewport[1],live.last_viewport[2],live.last_viewport[3],
                now[0],now[1],now[2],now[3],now[4],now[5]);
            live.last_viewport=now;
        }
        live.last_viewport_call=ordinal;live.last_viewport_caller=caller;
    }
    static bool VulkanPreviewSelected(){
#ifdef HULK_ENABLE_VULKAN_PREVIEW
        const char* v=std::getenv("HULK_RENDERER");return v&&std::strcmp(v,"vulkan")==0;
#else
        return false;
#endif
    }
    void DrainLivePush(LiveD3dDevice& live,bool flush_before_reuse=false){
        live.buffers.RefillScratch([&](uint32_t begin,uint32_t end){
        const bool trace=live.TraceGraphics();
        auto dump=[&]{
            for(auto m:DecodeNv2aMethods(mem_,begin,end))
                std::fprintf(stderr,"D3D LIVE PUSH method=%04X value=%08X\n",m.method,m.value);
        };
        if(trace)dump();
        try{
            live.bridge.ApplyPush(begin,end,VulkanPreviewSelected(),!trace);
            if(flush_before_reuse)live.backend.Flush();
        }
        catch(...){
            if(!trace){
                std::fprintf(stderr,"D3D LIVE PUSH FAILURE begin=%08X end=%08X\n",begin,end);
                try{dump();}catch(...){/* keep original failure */}
            }
            throw;
        }
        });
    }
    void SyncLiveTargets(LiveD3dDevice& live){
        if(!VulkanPreviewSelected()){
            live.buffers.VerifyInitialTargets();return;
        }
        live.bridge.SyncVulkanTargetMirrors(live.buffers.DeviceAddress());
        live.bridge.ValidateCapturedTargets();
    }
    void ReleaseD3dOwner(uint32_t thread){
        if(live_d3d_&&live_d3d_->owner==thread){
            live_d3d_->SaveLastPresented();
            live_d3d_.reset();
            std::fprintf(stderr,"D3D persistent diagnostic device released on owner thread\n");
        }
    }
    XbeD3dDefaultTrace* default_trace_=nullptr;
    uint32_t default_thread_=0;
    void DefaultProbe(X86Context& cpu,uint32_t routine,uint32_t device,uint32_t slot,uint32_t pp,
                      std::unique_lock<std::mutex>& lock){
        if(live_d3d_||default_trace_||!entry_||!routine||cpu.esp<4||!mem_.IsCommitted(cpu.esp-4,4))
            throw std::runtime_error("Invalid D3D default initializer entry");
        auto owned=std::make_unique<LiveD3dDevice>(mem_,pool_,physical_,cpu.thread_handle);
        auto& buffers=owned->buffers;
        buffers.Initialize(device,slot,pp);
        std::fprintf(stderr,"D3D INITIAL RASTER LIMITS: %u x %u\n",mem_.Read32(device+0x954),mem_.Read32(device+0x958));
        const char* fvf_text=std::getenv("HULK_FVF_SHADER");
        if(!fvf_text||std::strlen(fvf_text)!=8)throw std::runtime_error("Original FVF pointer not resolved; inspect local discovery report");
        for(unsigned i=0;i<8;++i)if(!((fvf_text[i]>='0'&&fvf_text[i]<='9')||
            (fvf_text[i]>='A'&&fvf_text[i]<='F')||(fvf_text[i]>='a'&&fvf_text[i]<='f')))
            throw std::runtime_error("Invalid original FVF pointer");
        auto fvf=uint32_t(std::strtoul(fvf_text,nullptr,16));
        buffers.BindInitialFvf(fvf);
        std::fprintf(stderr,"D3D INITIAL FVF: object=%08X flags=%08X\n",fvf,mem_.Read32(fvf+4));
        XbeD3dDefaultTrace trace(mem_);
        X86Context nested=cpu;nested.esp-=4;mem_.Write32(nested.esp,0);
        default_trace_=&trace;default_thread_=cpu.thread_handle;
        auto old_ordinal=fault_ordinal,old_return=fault_return,old_stack=fault_stack;
        std::fprintf(stderr,"D3D DEFAULT INIT: entry=%08X scratch=%08X\n",routine,buffers.Scratch());
        lock.unlock();
        int status=1;
        try{status=entry_(&nested,mem_.Base(),routine,Dispatch);}
        catch(...){lock.lock();default_trace_=nullptr;default_thread_=0;throw;}
        lock.lock();default_trace_=nullptr;default_thread_=0;
        fault_ordinal=old_ordinal;fault_return=old_return;fault_stack=old_stack;
        trace.Dump(buffers);
        if(status||nested.esp!=cpu.esp)throw std::runtime_error("D3D original default initializer stopped or violated stack ABI");
        std::fprintf(stderr,"D3D ORIGINAL DEFAULT INIT RETURNED\n");
        // Bind the *same* original guest device allocations to the GLES bridge,
        // not a fresh scratch-only device with synthetic render defaults.
        auto& backend=owned->backend;
        auto& bridge=owned->bridge;
        auto w=mem_.Read32(device+0xee8),h=mem_.Read32(device+0xeec);
        bridge.Initialize(w,h);
        bridge.BindInitialTargets(buffers.BackBuffer(0),buffers.Depth());
        bridge.AdoptOriginalDefaults(trace.State(),buffers.Scratch(),buffers.ScratchEnd());
        auto live=bridge.Snapshot();
        if(live.rs!=trace.State().rs||live.tss!=trace.State().tss)
            throw std::runtime_error("Original D3D state handoff mismatch");
        buffers.RewindScratch();
        gpu::ScopedGuestMemory scope(mem_);
        backend.Clear(live,0,nullptr,0xf0u|(buffers.Depth()?gpu::kClearZ:0u),0xff123456,1.0f,0);
        auto pixel=backend.ReadPixel(w/2,h/2);
        if(pixel!=std::array<uint8_t,4>{0x12,0x34,0x56,0xff})
            throw std::runtime_error("Original-state target clear mismatch");
        backend.ReadbackColor(live.rt);
        gpu::TextureDesc desc{};
        if(!gpu::DescribeSurface(live.rt,desc)||mem_.Read32(desc.data+(h/2)*desc.pitch+(w/2)*4)!=0xff123456)
            throw std::runtime_error("Original-state guest readback mismatch");
        std::fprintf(stderr,"D3D ORIGINAL STATE HANDOFF PASS: color=%08X depth=%08X z=%u\n",
            live.rt.header,live.zs.header,live.rs[gpu::kRsZEnable]);
        TraceViewport(*owned,kGraphicsCreate,mem_.Read32(cpu.esp));
        live_d3d_=std::move(owned);
    }
#endif
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
        w.pcr=slot; w.handle=handle; w.id=id; w.stack_pages=uint32_t(committed/4096);
        thread_settings_.emplace(slot+0x400,ThreadSettings{});
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
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
            ReleaseD3dOwner(child.thread_handle);
#endif
            AbandonMutants(child.thread_handle);
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
        if(dpc_running_||dpc_queue_.empty()||Irql(cpu)>=2)return;
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
        auto old_irql=Irql(cpu);Irql(cpu)=2;
        auto old_ordinal=fault_ordinal,old_return=fault_return,old_stack=fault_stack;
        lock.unlock();
        int status=0;
        try{status=entry_(&nested,mem_.Base(),routine,Dispatch);}
        catch(...){lock.lock();Irql(cpu)=old_irql;dpc_running_=false;cv_.notify_all();throw;}
        lock.lock();Irql(cpu)=old_irql;dpc_running_=false;cv_.notify_all();
        fault_ordinal=old_ordinal;fault_return=old_return;fault_stack=old_stack;
        if(status||nested.esp!=cpu.esp)throw std::runtime_error("DPC guest callback failed or violated stack ABI");
    }
    // KeInitializeEvent may be inlined by the original XDK. Accept only
    // complete, idle dispatcher headers, never zero-filled device storage.
    bool InlineEvent(uint32_t body){
        if(!body||body%4||!mem_.IsCommitted(body,16))return false;
        const auto header=mem_.Read32(body);
        return (header==0x00040000u||header==0x00040001u)&&
            mem_.Read32(body+4)<=1&&mem_.Read32(body+8)==body+8&&
            mem_.Read32(body+12)==body+8;
    }
    bool PointerObject(uint32_t body){
        if(!body||!mem_.IsCommitted(body,16))return false;
        uint32_t header=mem_.Read32(body),type=header&255u;
        if(thread_settings_.count(body))return true;
        if(object_references_.count(body)&&object_references_.at(body))return true;
        if(mutants_.count(body))return type==2;
        if(timer_types_.count(body))return type==8+timer_types_.at(body);
        if(pointer_events_.count(body))return type==pointer_events_.at(body);
        for(const auto& item:events_)if(item.second.body==body)return true;
        for(size_t i=0;i<count_;++i)if(workers_[i].pcr+0x400==body)return true;
        return InlineEvent(body);
    }
    uint32_t Body(uint32_t h) {
        auto it=events_.find(h);
        if(it!=events_.end()) return it->second.body;
        auto* w=Find(h);
        if(w) return w->pcr+0x400;
        return 0;
    }
    bool Ready(uint32_t body,uint32_t thread)const{
        auto it=mutants_.find(body);
        if(it!=mutants_.end())return !it->second.depth||it->second.owner==thread;
        return mem_.Read32(body+4)!=0;
    }
    void SyncMutant(uint32_t body,const Mutant& m){
        mem_.Write32(body+4,1u-m.depth);
        mem_.Write32(body+24,m.depth?ThreadBody(m.owner):0);
        const uint8_t abandoned=m.abandoned?1:0;
        mem_.Write(body+28,&abandoned,1);
    }
    uint32_t ThreadBody(uint32_t thread)const{
        for(size_t i=0;i<count_;++i)if(workers_[i].handle==thread)return workers_[i].pcr+0x400;
        return 0;
    }
    uint32_t Consume(uint32_t body,uint32_t thread){
        auto it=mutants_.find(body);
        if(it!=mutants_.end()){
            auto& m=it->second;
            if(m.depth>=0x7fffffffu)throw std::runtime_error("Mutant recursion limit");
            auto result=m.abandoned?0x80u:0u;
            m.owner=thread;++m.depth;m.abandoned=false;SyncMutant(body,m);return result;
        }
        auto type=mem_.Read32(body)&255u;
        if(type==1||type==9)mem_.Write32(body+4,0);
        else if(type==5)mem_.Write32(body+4,mem_.Read32(body+4)-1);
        return 0;
    }
    uint32_t ReleaseMutant(uint32_t body,uint32_t thread,uint32_t out){
        auto it=mutants_.find(body);
        if(it==mutants_.end())return 0xc0000024u;
        auto& m=it->second;
        if(!m.depth||m.owner!=thread)return 0xc0000046u;
        if(out&&!mem_.IsCommitted(out,4))return 0xc000000du;
        Output(out,1u-m.depth);
        if(!--m.depth)m.owner=0;
        SyncMutant(body,m);cv_.notify_all();return 0;
    }
    void AbandonMutants(uint32_t thread){
        for(auto& [body,m]:mutants_)if(m.depth&&m.owner==thread){
            m.depth=0;m.owner=0;m.abandoned=true;SyncMutant(body,m);
        }
    }
    uint32_t WaitBody(X86Context& cpu,uint32_t body,uint32_t timeout,
                      std::unique_lock<std::mutex>& lock) {
        if(!body) return 0xc0000008u;
        const auto deadline=Deadline(timeout);
        for(;;){
            Pause(cpu,lock);ServiceDpc(cpu,lock);
            if(Ready(body,cpu.thread_handle))break;
            if(Steady::now()>=deadline)return 0x102;
            if(!dpc_running_&&!dpc_queue_.empty()&&Irql(cpu)<2)continue;
            cv_.wait_until(lock,NextTimer(deadline));
        }
        return Consume(body,cpu.thread_handle);
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
    std::string DiagnosticString(uint32_t address,size_t max){
        if(!address)return "<null>";
        std::string out;
        for(size_t i=0;i<max;++i){
            uint64_t a=uint64_t(address)+i;
            if(a>=XboxGuestAddressSpace::kSize||!mem_.IsCommitted(uint32_t(a),1)){
                out+="<unmapped>";return out;
            }
            char c=char(mem_.Base()[a]);
            if(!c)return out;
            out+=c>=32&&c<127?c:'?';
        }
        out+="<truncated>";return out;
    }
    void DumpRecent(){
        std::fprintf(stderr,"Recent guest kernel/host calls (ordinal, thread, return VA, EAX):\n");
        const uint64_t begin=trace_seq_>recent_.size()?trace_seq_-recent_.size():0;
        for(uint64_t seq=begin;seq<trace_seq_;++seq){
            const auto& r=recent_[seq%recent_.size()];
            if(r.seq!=seq)continue;
            std::fprintf(stderr,"  %u thread=%08X ret=%08X esp=%08X %s=%08X\n",
                r.ordinal,r.thread,r.ret,r.esp,r.returned?"result":"entry-eax",r.eax);
        }
    }
    void ServiceSoundDone(X86Context& cpu,std::unique_lock<std::mutex>& lock){
        if(sound_callback_running_)return;
        HulkSound::StreamDone d{};
        for(unsigned i=0;i<1024&&sound_.TakeStreamDone(d);++i){
            if(!d.callback){
                auto e=events_.find(d.packet_context);
                if(e==events_.end()||semaphore_limits_.count(e->second.body)||timer_types_.count(e->second.body)||mutants_.count(e->second.body))
                    throw std::runtime_error("Invalid stream completion event");
                mem_.Write32(e->second.body+4,1);cv_.notify_all();continue;
            }
            if(!entry_||!mem_.IsCommitted(d.callback,1)||cpu.esp<16||!mem_.IsCommitted(cpu.esp-16,16))
                throw std::runtime_error("Invalid stream completion callback");
            X86Context nested=cpu;nested.esp-=16;
            const uint32_t args[]={0,d.context,d.packet_context,d.status};
            mem_.Write(nested.esp,args,sizeof(args));sound_callback_running_=true;
            auto old_ordinal=fault_ordinal,old_return=fault_return,old_stack=fault_stack;
            lock.unlock();int result=0;
            try{result=entry_(&nested,mem_.Base(),d.callback,Dispatch);}
            catch(...){lock.lock();sound_callback_running_=false;throw;}
            lock.lock();sound_callback_running_=false;
            fault_ordinal=old_ordinal;fault_return=old_return;fault_stack=old_stack;
            if(result||nested.esp!=cpu.esp)throw std::runtime_error("Stream callback violated guest ABI");
        }
    }
    void Call(X86Context& cpu,uint32_t ordinal,std::unique_lock<std::mutex>& lock) {
        if(preview_host.enabled.load()&&preview_host.stop.load())
            throw std::runtime_error("Android preview stop requested");
        Pause(cpu,lock);
        // XGetDevices can inspect the guest record without an input hook.
        // Publish hotplug state at ordinary guest boundaries as well as swaps,
        // including during loading or when the title has not opened a pad.
        if(ordinal!=kXapiInputGetState&&ordinal!=kXapiInputGetCapabilities&&
           ordinal!=kXapiInputSetState)RefreshPreviewPorts();
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
        // Advance wall-clock audio even while the guest is rendering or
        // doing I/O rather than explicitly calling DirectSoundDoWork.
        // The existing session lock serializes all producers; callbacks
        // remain on the original guest sound dispatch path.
        if(preview_host.enabled.load()&&VulkanPreviewSelected())sound_.Pump();
        if(live_d3d_&&live_d3d_->owner==cpu.thread_handle)
            TraceViewport(*live_d3d_,ordinal,mem_.IsCommitted(cpu.esp,4)?mem_.Read32(cpu.esp):0);
        if(ordinal==kDefaultSimple||ordinal==kDefaultRender||ordinal==kDefaultTexture||
            (ordinal>=kDefaultComplexBase&&ordinal<kDefaultComplexBase+gpu::kRsMax)){
            const unsigned n=ordinal==kDefaultSimple?0:ordinal==kDefaultRender?2:ordinal==kDefaultTexture?3:1;
            Check(cpu,n);
            auto apply=[&](XbeD3dDefaultTrace& trace){
                if(ordinal==kDefaultSimple)trace.Simple(cpu.ecx,cpu.edx);
                else if(ordinal==kDefaultRender)trace.Render(cpu.ecx,cpu.edx,Arg(cpu,0),Arg(cpu,1));
                else if(ordinal==kDefaultTexture)trace.Texture(cpu.ecx,Arg(cpu,0),Arg(cpu,1),Arg(cpu,2));
                else trace.Render(0,cpu.edx,ordinal-kDefaultComplexBase,Arg(cpu,0));
            };
            if(default_trace_){
                if(default_thread_!=cpu.thread_handle)throw std::runtime_error("Concurrent D3D default setter");
                apply(*default_trace_);
            }else{
                if(!live_d3d_||live_d3d_->owner!=cpu.thread_handle)
                    throw std::runtime_error("D3D state setter without owned live device");
                auto& live=*live_d3d_;
                if(live.SetterLimitReached())
                    throw std::runtime_error("D3D live setter budget exceeded");
                ++live.setter_calls;
                if(live.TraceGraphics()){
                    std::fprintf(stderr,"D3D LIVE SETTER caller=%08X id=%08X ECX=%08X EDX=%08X stack_args=%u\n",
                        mem_.Read32(cpu.esp),ordinal,cpu.ecx,cpu.edx,n);
                    for(unsigned i=0;i<n;++i)std::fprintf(stderr,"D3D LIVE ARG %u=%08X\n",i,Arg(cpu,i));
                }
                // Preserve ordering with direct guest writes. Only supported
                // state-only packets may be consumed; unknown ones stop.
                DrainLivePush(live);
                XbeD3dDefaultTrace trace(mem_,live.bridge.Snapshot(),live.TraceGraphics());
                apply(trace);
                live.bridge.AdoptLiveSetters(trace.State());
            }
            Ret(cpu,n,0);return;
        }
#endif
        switch(ordinal) {
            case kSoundMixBinHeadroom:Check(cpu,3);sound_.Pump();Ret(cpu,3,sound_.SetMixBinHeadroom(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2)));return;
            case kSoundI3dl2Listener:Check(cpu,3);sound_.Pump();Ret(cpu,3,sound_.I3dl2Listener(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2)));return;
            case kSoundListenerPosition:case kSoundListenerVelocity:case kSoundListenerOrientation:
            case kSoundListenerDistance:case kSoundListenerRolloff:case kSoundListenerDoppler:case kSoundCommit3d:{
                unsigned n=ordinal==kSoundCommit3d?1:ordinal==kSoundListenerOrientation?8:
                    (ordinal==kSoundListenerPosition||ordinal==kSoundListenerVelocity)?5:3;
                Check(cpu,n);std::array<uint32_t,8> a{};for(unsigned i=0;i<n;++i)a[i]=Arg(cpu,i);
                sound_.Pump();Ret(cpu,n,sound_.Listener3d(a[0],ordinal,a));return;
            }
            case kSoundCreate:Check(cpu,3);Ret(cpu,3,sound_.Create(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2)));return;
            case kSoundReleaseDevice:Check(cpu,1);Ret(cpu,1,sound_.ReleaseDevice(Arg(cpu,0)));return;
            case kSoundSpeaker:Check(cpu,2);Ret(cpu,2,sound_.Speaker(Arg(cpu,0),Arg(cpu,1)));return;
            case kSoundDownloadEffects:Check(cpu,5);Ret(cpu,5,sound_.DownloadEffects(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4)));return;
            case kSoundCommitEffectData:Check(cpu,1);sound_.Pump();Ret(cpu,1,sound_.CommitEffectData(Arg(cpu,0)));return;
            case kSoundGetEffectData:Check(cpu,5);Ret(cpu,5,sound_.GetEffectData(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4)));return;
            case kSoundFullHrtf:Check(cpu,0);sound_.SelectFullHrtf();Ret(cpu,0,0);return;
            case kSoundSetEffectData:Check(cpu,6);Ret(cpu,6,sound_.SetEffectData(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4),Arg(cpu,5)));return;
            case kSoundSynchPlayback:Check(cpu,1);sound_.Pump();Ret(cpu,1,sound_.SynchPlayback(Arg(cpu,0)));return;
            case kSoundPump:Check(cpu,0);sound_.Pump();Ret(cpu,0,0);ServiceSoundDone(cpu,lock);return;
            case kSoundNewStream:case kSoundNewDeviceStream:{
                unsigned n=ordinal==kSoundNewStream?2:4;Check(cpu,n);
                if(n==4&&(!sound_.Device(Arg(cpu,0))||Arg(cpu,3))){Ret(cpu,n,0x88780032u);return;}
                Ret(cpu,n,sound_.NewStream(Arg(cpu,n==2?0:1),Arg(cpu,n==2?1:2),cpu.ecx,cpu.edx));return;
            }
            case kSoundStreamI3dl2Source:
            case kSoundStreamSpatialPosition:case kSoundStreamVelocity:case kSoundStreamMinDistance:case kSoundStreamMaxDistance:
            case kSoundStreamAddRef:case kSoundStreamRelease:case kSoundStreamInfo:
            case kSoundStreamStatus:case kSoundStreamProcess:case kSoundStreamDiscontinuity:
            case kSoundStreamFlush:case kSoundStreamFlushEx:case kSoundStreamPause:
            case kSoundStreamVolume:case kSoundStreamFrequency:case kSoundStreamHeadroom:case kSoundStreamMixBins:case kSoundStreamMixBinVolumes:case kSoundStreamFormat:{
                unsigned n=(ordinal==kSoundStreamSpatialPosition||ordinal==kSoundStreamVelocity)?5:ordinal==kSoundStreamFlushEx?4:
                    (ordinal==kSoundStreamProcess||ordinal==kSoundStreamMinDistance||ordinal==kSoundStreamMaxDistance||ordinal==kSoundStreamI3dl2Source)?3:
                    (ordinal==kSoundStreamAddRef||ordinal==kSoundStreamRelease||ordinal==kSoundStreamDiscontinuity||ordinal==kSoundStreamFlush)?1:2;
                Check(cpu,n);std::array<uint32_t,8> a{};for(unsigned i=0;i<n;++i)a[i]=Arg(cpu,i);
                if(ordinal==kSoundStreamProcess&&!sound_.HasStreamCallback(a[0])&&a[1]&&mem_.IsCommitted(a[1],20)){
                    auto e=mem_.Read32(a[1]+16);
                    if(e&&(!events_.count(e)||semaphore_limits_.count(events_.at(e).body)||timer_types_.count(events_.at(e).body)||mutants_.count(events_.at(e).body))){
                        Ret(cpu,n,0x88780032u);return;
                    }
                }
                sound_.Pump();auto result=sound_.Stream(a[0],ordinal,a);Ret(cpu,n,result);
                if(ordinal==kSoundStreamFlush||ordinal==kSoundStreamFlushEx||ordinal==kSoundStreamRelease||ordinal==kSoundStreamFormat)
                    ServiceSoundDone(cpu,lock);
                return;
            }
            case kSoundNewBuffer:Check(cpu,2);Ret(cpu,2,sound_.NewBuffer(Arg(cpu,0),Arg(cpu,1)));return;
            case kSoundNewDeviceBuffer:Check(cpu,4);Ret(cpu,4,sound_.Device(Arg(cpu,0))&&!Arg(cpu,3)?sound_.NewBuffer(Arg(cpu,1),Arg(cpu,2)):0x88780032u);return;
            case kSoundBufferAll3d:case kSoundBufferRolloffCurve:case kSoundBufferStopEx:
            case kSoundBufferI3dl2Source:
            case kSoundBufferSpatialPosition:case kSoundBufferVelocity:case kSoundBufferMinDistance:case kSoundBufferMaxDistance:
            case kSoundBufferRelease:
            case kSoundBufferData:
            case kSoundBufferPlay:
            case kSoundBufferStop:
            case kSoundBufferStatus:
            case kSoundBufferVolume:
            case kSoundBufferFrequency:
            case kSoundBufferSetPosition:
            case kSoundBufferLock:
            case kSoundBufferUnlock:
            case kSoundBufferHeadroom:
            case kSoundBufferMixBins:
            case kSoundBufferMixBinVolumes:
            case kSoundBufferFormat:
            case kSoundBufferPause:
            case kSoundBufferPlayRegion:
            case kSoundBufferLoopRegion:
            case kSoundBufferGetPosition: {
                unsigned n=ordinal==kSoundBufferLock?8:(ordinal==kSoundBufferUnlock||ordinal==kSoundBufferSpatialPosition||ordinal==kSoundBufferVelocity)?5:(ordinal==kSoundBufferPlay||ordinal==kSoundBufferRolloffCurve||ordinal==kSoundBufferStopEx)?4:
                    (ordinal==kSoundBufferData||ordinal==kSoundBufferGetPosition||ordinal==kSoundBufferPlayRegion||ordinal==kSoundBufferLoopRegion||ordinal==kSoundBufferMinDistance||ordinal==kSoundBufferMaxDistance||ordinal==kSoundBufferI3dl2Source||ordinal==kSoundBufferAll3d)?3:
                    (ordinal==kSoundBufferRelease||ordinal==kSoundBufferStop)?1:2;
                Check(cpu,n);
                sound_.Pump();
                std::array<uint32_t,8> a{};
                for(unsigned i=0;i<n;++i)a[i]=Arg(cpu,i);
                Ret(cpu,n,sound_.Buffer(a[0],ordinal,a));return;
            }
            case kGraphicsRefillScratch:{
                Check(cpu,0);
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
                if(!VulkanPreviewSelected()||!live_d3d_||default_trace_||
                   live_d3d_->owner!=cpu.thread_handle)
                    throw std::runtime_error("D3D scratch-space refill outside live Vulkan owner");
                auto& live=*live_d3d_;
                // Pinned SCDA D3DDevice_MakeSpace stdcall(0) returns the
                // scratch base. Unlike that implementation, parse pending
                // state packets before rewinding and flush queued draws.
                if(live.SetterLimitReached())throw std::runtime_error("D3D scratch refill budget exceeded");
                ++live.setter_calls;
                live.backend.Flush();
                DrainLivePush(live,true);
                if(live.TraceGraphics())std::fprintf(stderr,"D3D VULKAN SCRATCH REFILL base=%08X\n",live.buffers.Scratch());
                Ret(cpu,0,live.buffers.Scratch());return;
#else
                throw std::runtime_error("D3D scratch-space refill requires live backend");
#endif
            }
            case kDefaultMakeSpace:throw std::runtime_error("D3D requested-space ABI not integrated");
            case kXapiInitDevices: {
                Check(cpu,2);
                auto pad=cpu.ecx,mu=cpu.edx;
                // XDK 5849 layout.toml: Current +0, Changed +4, Previous +8.
                if(!pad||pad%4||!mem_.IsCommitted(pad,12)||
                    (mu&&(mu%4||!mem_.IsCommitted(mu,12)||
                    (uint64_t(mu)<uint64_t(pad)+12&&uint64_t(pad)<uint64_t(mu)+12))))
                    throw std::runtime_error("Invalid generated XPP device records");
                if(gamepad_type_&&(gamepad_type_!=pad||memory_unit_type_!=mu))
                    throw std::runtime_error("XPP device records changed");
                gamepad_type_=pad;memory_unit_type_=mu;
                pad_script_.Configure(std::getenv("HULK_INPUT_SCRIPT"),DiagnosticFrameLimit());
                const uint32_t zero[3]={};
                const bool connected=preview_host.enabled.load()?preview_host.Poll().first:pad_script_.Enabled();
                const uint32_t ports[3]={uint32_t(connected),uint32_t(connected),0};
                mem_.Write(pad,ports,sizeof(ports));
                if(mu)mem_.Write(mu,zero,sizeof(zero));
                std::fprintf(stderr,"HULK INPUT initialization: pad=%08X memory_unit=%08X connected=%u provider=%s\n",
                    pad,mu,ports[0],preview_host.enabled.load()?"Android physical input":pad_script_.Enabled()?"scripted diagnostic":"none");
                Ret(cpu,2,0);return;
            }
            case kXapiInputOpen: {
                Check(cpu,4);
                if(!gamepad_type_||Arg(cpu,0)!=gamepad_type_||Arg(cpu,1)>3){
                    Ret(cpu,4,0);return;
                }
                if(input_handles_.size()>=64){Ret(cpu,4,0);return;}
                // Match upstream's per-port handle even if no device is connected.
                auto h=pool_.Allocate(16);
                if(h)input_handles_.emplace(h,InputHandle{Arg(cpu,1)});
                Ret(cpu,4,h);return;
            }
            case kXapiInputClose: {
                Check(cpu,1);auto h=Arg(cpu,0);
                if(input_handles_.erase(h))pool_.Free(h);
                Ret(cpu,1,0);return;
            }
            case kXapiInputGetState: case kXapiInputSetState: case kXapiInputGetCapabilities: {
                Check(cpu,2);
                auto it=input_handles_.find(Arg(cpu,0));
                if(it==input_handles_.end()){Ret(cpu,2,6);return;}
                auto& h=it->second;
                const auto physical=preview_host.Poll();
                const bool live_input=preview_host.enabled.load();
                // Publish the same snapshot used to answer this call.
                if(live_input)RefreshPreviewPorts(physical.first);
                if(h.port!=0||!(live_input?physical.first:pad_script_.Enabled())){Ret(cpu,2,1167);return;}
                const auto out=Arg(cpu,1);
                if(ordinal==kXapiInputGetState){
                    if(!out||!mem_.IsCommitted(out,22)){Ret(cpu,2,87);return;}
                    auto state=live_input?physical.second:pad_script_.At(input_frame_);
                    if(!h.polled||h.last!=state){
                        ++h.packet;h.last=state;h.polled=true;
                        std::fprintf(stderr,"HULK INPUT state frame=%u packet=%u buttons=%02X analogA=%u analogB=%u lx=%02X%02X ly=%02X%02X\n",
                            input_frame_,h.packet,unsigned(state[0]),unsigned(state[2]),unsigned(state[3]),
                            unsigned(state[11]),unsigned(state[10]),unsigned(state[13]),unsigned(state[12]));
                    }
                    auto packet=HulkPadScript::Packet(h.packet,state);
                    mem_.Write(out,packet.data(),packet.size());Ret(cpu,2,0);return;
                }
                if(ordinal==kXapiInputGetCapabilities){
                    auto caps=HulkPadScript::Capabilities();
                    if(!out||!mem_.IsCommitted(out,caps.size())){Ret(cpu,2,87);return;}
                    mem_.Write(out,caps.data(),caps.size());Ret(cpu,2,0);return;
                }
                // Script is not a real vibration device; do not fake completion.
                Ret(cpu,2,50);return;
            }
            case kMissingGuestEntry: {
                Check(cpu,0);
                std::fprintf(stderr,"MISSING GUEST target=%08X caller=%08X ESP=%08X EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X\n",
                    cpu.last_icall,mem_.Read32(cpu.esp),cpu.esp,cpu.eax,cpu.ebx,cpu.ecx,cpu.edx,cpu.esi,cpu.edi);
                for(unsigned i=0;i<12;++i){
                    uint64_t a=uint64_t(cpu.esp)+i*4;
                    if(a+4>XboxGuestAddressSpace::kSize||!mem_.IsCommitted(uint32_t(a),4))break;
                    std::fprintf(stderr,"Missing guest stack +%02X=%08X\n",i*4,mem_.Read32(uint32_t(a)));
                }
                DumpRecent();
                throw std::runtime_error("Unmapped Xbox guest entry; target and caller captured");
            }
            case kHulkAllocatorStop: {
                Check(cpu,0);
                const auto ret=mem_.Read32(cpu.esp);
                std::fprintf(stderr,"HULK STOP target=0008D320 ret=%08X ESP=%08X EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X\n",
                    ret,cpu.esp,cpu.eax,cpu.ebx,cpu.ecx,cpu.edx,cpu.esi,cpu.edi);
                // Only decode the USA allocator wrapper ABI when its
                // exact return address matches the inspected source.
                if(ret==0x00266E1Cu&&mem_.IsCommitted(cpu.esp,32)){
                    auto type=mem_.Read32(cpu.esp+24),bytes=mem_.Read32(cpu.esp+28);
                    auto caller=mem_.Read32(cpu.esp+20);
                    std::fprintf(stderr,"HULK ALLOC caller=%08X type=%08X bytes=%08X vcall_arg=%08X\n",
                        caller,type,bytes,mem_.Read32(cpu.esp+4));
                    uint64_t slot=0x00527C60ull+uint64_t(type)*16;
                    if(slot+16<=XboxGuestAddressSpace::kSize&&mem_.IsCommitted(uint32_t(slot),16)){
                        std::fprintf(stderr,"Allocator slot=%08X values=%08X %08X %08X %08X\n",uint32_t(slot),
                            mem_.Read32(uint32_t(slot)),mem_.Read32(uint32_t(slot+4)),
                            mem_.Read32(uint32_t(slot+8)),mem_.Read32(uint32_t(slot+12)));
                    }
                    if(mem_.IsCommitted(cpu.ecx,4)){
                        auto table=mem_.Read32(cpu.ecx);
                        if(mem_.IsCommitted(table,16))
                            std::fprintf(stderr,"Allocator object=%08X vtable=%08X entries=%08X %08X %08X %08X\n",
                                cpu.ecx,table,mem_.Read32(table),mem_.Read32(table+4),mem_.Read32(table+8),mem_.Read32(table+12));
                    }
                }
                if(ret==0x00266E38u&&mem_.IsCommitted(cpu.esp,44)){
                    std::fprintf(stderr,"HULK ALLOC caller=%08X type=%08X bytes=%08X failure_context=%08X reason=%08X\n",
                        mem_.Read32(cpu.esp+32),mem_.Read32(cpu.esp+8),mem_.Read32(cpu.esp+12),
                        mem_.Read32(cpu.esp+4),mem_.Read32(cpu.esp+16));
                }
                for(unsigned i=0;i<16;++i){
                    uint64_t a=uint64_t(cpu.esp)+i*4;
                    if(a+4>XboxGuestAddressSpace::kSize||!mem_.IsCommitted(uint32_t(a),4))break;
                    std::fprintf(stderr,"Hulk stack +%02X=%08X\n",i*4,mem_.Read32(uint32_t(a)));
                }
                DumpRecent();
                throw std::runtime_error("Hulk entered deliberate stop loop; allocator caller captured");
            }
            case kXapiLaunchImage: {
                Check(cpu,2);auto path=Arg(cpu,0);
                auto name=DiagnosticString(path,520);
                std::fprintf(stderr,"XLaunchNewImageA request: path=%s data=%08X caller=%08X\n",
                    name.c_str(),Arg(cpu,1),mem_.Read32(cpu.esp));
                // Follow the pinned XDK runtime: refuse launch of another
                // image not available as a recompiled executable.
                // Do not silently claim a restart of this title succeeded.
                std::string upper=name;
                for(auto& c:upper){if(c>='a'&&c<='z')c=char(c-32);if(c=='/')c='\\';}
                if(upper=="D:\\DEFAULT.XBE"||upper=="\\DEVICE\\CDROM0\\DEFAULT.XBE"||
                   upper=="\\??\\D:\\DEFAULT.XBE"||upper=="DEFAULT.XBE"){
                    DumpRecent();throw std::runtime_error("Self-relaunch requested; quick reboot is not integrated");
                }
                Ret(cpu,2,2);return; // ERROR_FILE_NOT_FOUND
            }
            // XboxXBERecomp's high-level I/O runtime deliberately has no
            // low-level Xbox device drivers. Report failure, not a fake device.
            // ABI cross-checked against Cxbx-Reloaded EmuKrnlIo.cpp.
            case 65: {
                Check(cpu,6);auto out=Arg(cpu,5);
                if(!out||!mem_.IsCommitted(out,4)){Ret(cpu,6,0xc000000du);return;}
                mem_.Write32(out,0);
                std::fprintf(stderr,"IoCreateDevice unavailable: driver=%08X extension=%u name=%08X type=%08X exclusive=%u out=%08X status=C0000001\n",
                    Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4)&255u,out);
                Ret(cpu,6,0xc0000001u);return;
            }
            case 84: {
                Check(cpu,8);
                auto out=Arg(cpu,4),size=Arg(cpu,5),returned=Arg(cpu,6);
                if((size&&(!out||!mem_.IsCommitted(out,size)))||
                   (returned&&!mem_.IsCommitted(returned,4))){
                    Ret(cpu,8,0xc000000du);return;
                }
                if(size>0x01000000u)throw std::runtime_error("I/O output exceeds diagnostic budget");
                if(size)std::memset(mem_.Base()+out,0,size);
                Output(returned,0);
                std::fprintf(stderr,"IoSynchronousDeviceIoControlRequest unavailable: code=%08X device=%08X\n",Arg(cpu,0),Arg(cpu,1));
                Ret(cpu,8,0xc0000001u);return;
            }
            case 85: {
                Check(cpu,5);
                std::fprintf(stderr,"IoSynchronousFsdRequest unavailable: major=%08X device=%08X\n",Arg(cpu,0),Arg(cpu,1));
                Ret(cpu,5,0xc0000001u);return;
            }
            case 44: {
                Check(cpu,2);auto level=Arg(cpu,0),out=Arg(cpu,1);
                if(level>15||(out&&!mem_.IsCommitted(out,1)))
                    throw std::runtime_error("Unsupported interrupt vector request");
                // Metadata only: the primary vector base is 0x30.
                // This does not connect an interrupt or emulate a PIC.
                auto irql=uint8_t(level);
                if(out)mem_.Write(out,&irql,1);
                Ret(cpu,2,level+0x30u);return;
            }
            case 49: {
                Check(cpu,1);
                auto reason=Arg(cpu,0);
                std::fprintf(stderr,"HalReturnToFirmware reason=%u caller=%08X\n",reason,mem_.Read32(cpu.esp));
                auto slot=registry_?registry_->Resolve(164).guest_data:0;
                auto page=slot&&mem_.IsCommitted(slot,4)?mem_.Read32(slot):0;
                std::fprintf(stderr,"LaunchDataPage=%08X\n",page);
                if(page&&mem_.IsCommitted(page,4096)){
                    auto path=DiagnosticString(page+8,520);
                    std::fprintf(stderr,"Launch header type=%08X title=%08X path=%s flags=%08X\n",
                        mem_.Read32(page),mem_.Read32(page+4),path.c_str(),mem_.Read32(page+528));
                }
                DumpRecent();
                // Nonreturning firmware routine: never pretend success, never
                // start Android apps, restart the process or clear user files.
                throw std::runtime_error("Guest requested firmware exit/relaunch; inspect launch path and caller");
            }
            case 95: {
                Check(cpu,1);std::fprintf(stderr,"KeBugCheck code=%08X caller=%08X\n",Arg(cpu,0),mem_.Read32(cpu.esp));
                DumpRecent();throw std::runtime_error("Guest kernel bugcheck");
            }
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
                const auto caller=mem_.Read32(cpu.esp);
                std::fprintf(stderr,"KERNEL POINTER WAIT caller=%08X object=%08X reason=%08X mode=%08X alert=%08X timeout=%08X thread=%08X\n",
                    caller,body,Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4),cpu.thread_handle);
                if(body&&mem_.IsCommitted(body,16))
                    std::fprintf(stderr,"KERNEL WAIT HEADER %08X %08X %08X %08X\n",
                        mem_.Read32(body),mem_.Read32(body+4),mem_.Read32(body+8),mem_.Read32(body+12));
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
                if(live_d3d_&&live_d3d_->owner==cpu.thread_handle){
                    const auto dev=live_d3d_->buffers.DeviceAddress();
                    if(body>=dev&&uint64_t(body)<uint64_t(dev)+XbeD3d8DeviceMemory::device_size)
                        std::fprintf(stderr,"KERNEL WAIT DEVICE OFFSET=%04X\n",body-dev);
                }
#endif
                if(!PointerObject(body))throw std::runtime_error("Unsupported dispatcher pointer wait");
                auto result=WaitBody(cpu,body,Arg(cpu,4),lock);
                std::fprintf(stderr,"KERNEL POINTER WAIT RETURN caller=%08X status=%08X\n",caller,result);
                Ret(cpu,5,result);return;
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
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
                if(!cpu.ecx||!cpu.edx)throw std::runtime_error("Missing generated D3D device symbols");
                auto out=Arg(cpu,5);
                if(!out||!mem_.IsCommitted(out,4))throw std::runtime_error("Invalid D3D device output");
                if(live_d3d_||default_trace_)throw std::runtime_error("Duplicate D3D device creation");
                if(Arg(cpu,0)!=0||Arg(cpu,1)!=1||Arg(cpu,2)!=0||Arg(cpu,3)!=0x40)
                    throw std::runtime_error("Unvalidated D3D device creation arguments");
                auto routine=cpu.eax,device=cpu.ecx,slot=cpu.edx;
                RunD3dCreateProbe(mem_,pool_,physical_,device,slot,pp);
                DefaultProbe(cpu,routine,device,slot,pp,lock);
                // Only the explicit diagnostic continuation may return this
                // partial device. Unsupported rendering remains a hard stop.
                auto opt=std::getenv("HULK_CONTINUE_CREATE");
                if(!opt||std::strcmp(opt,"1")!=0)
                    throw std::runtime_error("D3D persistent device ready; diagnostic continuation not enabled");
                if(!live_d3d_||live_d3d_->owner!=cpu.thread_handle||mem_.Read32(slot)!=device)
                    throw std::runtime_error("D3D persistent device ownership mismatch");
                mem_.Write32(out,device);
                std::fprintf(stderr,"D3D DIAGNOSTIC CREATE RETURN: device=%08X output=%08X caller=%08X (partial device, no gameplay claim)\n",
                    device,out,mem_.Read32(cpu.esp));
                Ret(cpu,6,0);return;
#else
                throw std::runtime_error("D3D8 CreateDevice reached; guest device initialization pending");
#endif
            }
            case kGraphicsClear: {
                Check(cpu,6);
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
                if(default_trace_||!live_d3d_||live_d3d_->owner!=cpu.thread_handle)
                    throw std::runtime_error("D3D clear without owned live device");
                auto& live=*live_d3d_;
                DrainLivePush(live);
                SyncLiveTargets(live);
                const bool sample_clear=!live.expanded_preview||!live.compact_capture||live.detailed_draws||
                    DiagnosticTracePoll(++live.clear_calls);
                if(sample_clear)std::fprintf(stderr,"D3D LIVE CLEAR caller=%08X rects=%u flags=%08X color=%08X z=%08X stencil=%08X\n",
                    mem_.Read32(cpu.esp),Arg(cpu,0),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4),Arg(cpu,5));
                live.bridge.Call("D3DDevice_Clear",cpu,D3dAbi::Stdcall(6));
                // Only the diagnostic sample needs immediate color readback.
                // Real presentation and resource sync retain their readbacks.
                if(sample_clear){
                    gpu::ScopedGuestMemory scope(mem_);
                    auto state=live.bridge.Snapshot();
                    live.backend.ReadbackColor(state.rt);
                    auto pixel=live.backend.ReadPixel(0,0);
                    std::fprintf(stderr,"D3D LIVE CLEAR READBACK RGBA=%02X%02X%02X%02X\n",pixel[0],pixel[1],pixel[2],pixel[3]);
                }
                if(sample_clear)std::fprintf(stderr,"D3D LIVE CLEAR PASS (offscreen)\n");return;
#else
                throw std::runtime_error("D3D clear requires GLES backend");
#endif
            }
            case kGraphicsDraw|0x1000u: case kGraphicsDrawIndexed|0x1000u: {
                Check(cpu,3);
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
                if(default_trace_||!live_d3d_||live_d3d_->owner!=cpu.thread_handle)
                    throw std::runtime_error("D3D draw without owned live device");
                auto& live=*live_d3d_;
                if(!VulkanPreviewSelected())live.buffers.VerifyInitialTargets();
                auto dev=live.buffers.DeviceAddress();
                if(live.TraceGraphics())std::fprintf(stderr,"D3D FIRST DRAW caller=%08X prim=%u arg1=%08X arg2=%08X streams=%08X rs=%08X tss=%08X\n",
                    mem_.Read32(cpu.esp),Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),cpu.eax,cpu.ecx,cpu.edx);
                // Capture actual shader object, viewport, texture bindings and
                // streams before draining the scratch buffer; a pending packet
                // must not hide the rest of this first-draw evidence.
                const bool full_capture=!live.compact_capture||live.detailed_draws||!live.diagnostic_draw_returns;
                if(full_capture)for(auto off:{0x8u,0x1cu,0x784u,0x794u,0x7a0u,0x948u,0x95cu,0x960u,0xee0u,0xee4u,0xee8u,0xeecu,0xef0u,0xef4u,0xef8u,0xefcu})
                    std::fprintf(stderr,"D3D DEVICE +%04X=%08X\n",off,mem_.Read32(dev+off));
                if(full_capture)for(auto off:{0x784u,0x794u}){
                    auto p=mem_.Read32(dev+off);
                    if(p&&mem_.IsCommitted(p,off==0x794?0x114:64))
                        for(unsigned i=0;i<(off==0x794?69u:16u);++i)std::fprintf(stderr,"D3D SHADER %04X +%02X=%08X\n",off,4*i,mem_.Read32(p+4*i));
                }
                if(full_capture)for(unsigned st=0;st<4;++st){
                    auto p=mem_.Read32(dev+0xf88+4*st);
                    std::fprintf(stderr,"D3D TEXTURE %u=%08X palette=%08X\n",st,p,mem_.Read32(dev+0xf98+4*st));
                    if(p&&mem_.IsCommitted(p,20))
                        for(unsigned i=0;i<5;++i)std::fprintf(stderr,"D3D TEXTURE %u +%02X=%08X\n",st,4*i,mem_.Read32(p+4*i));
                }
                live.bridge.SyncGuestDrawTables(cpu.eax,cpu.ecx,cpu.edx);
                auto state=live.bridge.Snapshot();
                if(full_capture)for(unsigned i=0;i<16;++i){
                    auto st=state.streams[i];
                    std::fprintf(stderr,"D3D STREAM %u vb=%08X stride=%u\n",i,st.vb,st.stride);
                    if(st.vb)for(unsigned j=0;j<3;++j)std::fprintf(stderr,"D3D VB %u +%02X=%08X\n",i,j*4,mem_.Read32(st.vb+j*4));
                }
                if(full_capture)for(unsigned i=gpu::kRsDeferredFirst;i<gpu::kRsMax;++i)
                    std::fprintf(stderr,"D3D DRAW RS %u=%08X\n",i,state.rs[i]);
                if(full_capture)for(unsigned st=0;st<4;++st)for(unsigned i=0;i<gpu::kTssCount;++i)
                    std::fprintf(stderr,"D3D DRAW TSS %u %u=%08X\n",st,i,state.tss[st][i]);
                DrainLivePush(live);
                SyncLiveTargets(live);
                auto fvf=live.bridge.SyncFixedDiffuseShader(dev,VulkanPreviewSelected());
                bool indexed=ordinal==(kGraphicsDrawIndexed|0x1000u);
                // Keep prepared storage alive throughout this serialized
                // draw. State replay only filters raster/shader constants,
                // not the validated stream declaration or indices.
                gpu::Assembled prepared;
                auto vertices=live.bridge.VerifyGuestGeometry(Arg(cpu,0),indexed?0:Arg(cpu,1),
                    indexed?Arg(cpu,1):Arg(cpu,2),indexed?Arg(cpu,2):0,VulkanPreviewSelected()?&prepared:nullptr);
                if(live.TraceGraphics())std::fprintf(stderr,"D3D ORIGINAL GEOMETRY ASSEMBLED: fvf=%08X vertices=%u (not rendered)\n",fvf,vertices);
                auto synced=live.bridge.Snapshot();
                if(full_capture)for(auto slot:{2,3,256}){
                    std::fprintf(stderr,"D3D DRAW MATRIX %u",slot);
                    for(auto v:synced.transforms[slot])std::fprintf(stderr," %08X",std::bit_cast<uint32_t>(v));
                    std::fprintf(stderr,"\n");
                }
                const bool zero_viewport=!synced.viewport.w||!synced.viewport.h;
                const char* allow=std::getenv("HULK_CONTINUE_DRAW");
                const bool opted_in=allow&&std::strcmp(allow,"1")==0;
                if(synced.vs&&synced.vs->program.empty()&&!live.diagnostic_lit_returns){
                    std::fprintf(stderr,"D3D PACKED NORMAL GEOMETRY PASS vertices=%u lighting=%u specular=%u ambient=%08X diffuse_source=%u ambient_source=%u\n",
                        vertices,synced.rs[gpu::kRsLighting],synced.rs[gpu::kRsSpecularEnable],synced.rs[gpu::kRsAmbient],
                        synced.rs[gpu::kRsDiffuseMaterialSource],synced.rs[gpu::kRsAmbientMaterialSource]);
                    auto [material_seen,defined]=live.bridge.LightingCoverage();
                    std::fprintf(stderr,"D3D LIGHT COVERAGE material=%u defined=%02X enabled=%02X\n",unsigned(material_seen),defined,synced.light_enable);
                    std::fprintf(stderr,"D3D LIGHT MATERIAL");
                    for(float f:synced.material)std::fprintf(stderr," %08X",std::bit_cast<uint32_t>(f));
                    std::fprintf(stderr,"\n");
                    for(unsigned i=0;i<8;++i)if(defined&(1u<<i)){
                        std::fprintf(stderr,"D3D LIGHT SLOT %u",i);
                        for(auto word:synced.lights[i])std::fprintf(stderr," %08X",word);
                        std::fprintf(stderr,"\n");
                    }
                    // The bridge validates captured light coverage and the
                    // restricted combiner before drawing; never fake a light.
                
                }
                const auto frame_draws=live.diagnostic_draw_returns-live.draws_at_swap;
                const auto caller=mem_.Read32(cpu.esp),start=indexed?0u:Arg(cpu,1),count=indexed?Arg(cpu,1):Arg(cpu,2);
                const auto indices=indexed?Arg(cpu,2):0u;
                if(VulkanPreviewSelected()&&opted_in&&!zero_viewport){
                    if(frame_draws>=live.diagnostic_draw_limit)
                        throw std::runtime_error("Vulkan live draw budget reached");
                    // No hardcoded callsite or one-texture profile for
                    // nonzero-view Vulkan draws. The bridge validates the
                    // captured declaration, primitive, states and resources.
                    try{live.bridge.ReplayCapturedShaderDiagnostic(Arg(cpu,0),start,count,indices,true,&prepared);}
                    catch(...){
                        std::fprintf(stderr,"D3D VULKAN DRAW FAILURE frame=%u draw=%u caller=%08X prim=%u start=%u count=%u indices=%08X fvf=%08X ps=%08X\n",
                            live.diagnostic_swap_returns+1,frame_draws,caller,Arg(cpu,0),start,count,indices,fvf,synced.ps?synced.ps->handle:0);
                        for(unsigned st=0;st<4;++st){
                            std::fprintf(stderr,"D3D FAILURE TEXTURE stage=%u header=%08X palette=%08X\n",st,synced.textures[st],synced.palettes[st]);
                            for(unsigned i=0;i<gpu::kTssCount;++i)std::fprintf(stderr,"D3D FAILURE TSS %u %u=%08X\n",st,i,synced.tss[st][i]);
                        }
                        for(unsigned i=gpu::kRsDeferredFirst;i<gpu::kRsMax;++i)std::fprintf(stderr,"D3D FAILURE RS %u=%08X\n",i,synced.rs[i]);
                        throw;
                    }
                    ++live.diagnostic_draw_returns;live.diagnostic_vertices+=vertices;
                    if(synced.vs&&synced.vs->program.empty())++live.diagnostic_lit_returns;
                    if(!synced.ps)++live.diagnostic_texture_returns;
                    if(full_capture||frame_draws==0)
                        std::fprintf(stderr,"D3D VULKAN LIVE DRAW RETURN: vs=%08X instructions=%zu ps=%08X combiners=%u vertices=%u\n",
                            synced.vs_handle,synced.vs?synced.vs->program.size()/4:0,
                            synced.ps?synced.ps->handle:0,synced.ps?synced.ps->def[53]&255:0,vertices);
                    Ret(cpu,3,0);return;
                }
                if(((!indexed&&caller==0x0031c10f)||(indexed&&caller==0x00327371&&indices))&&(fvf==0x142||synced.vs)&&
                   ((Arg(cpu,0)==5&&count&&count%3==0)||(Arg(cpu,0)==6&&count>=3))){
                    if(frame_draws>=live.diagnostic_draw_limit)
                        throw std::runtime_error("Textured diagnostic draw budget reached");
                    // Validate each current texture/combiner/primitive independently
                    // instead of requiring every screen to match the copyright raster profile.
                    gpu::ScopedGuestMemory scope(mem_);
                    gpu::TextureDesc desc{};
                    if(!gpu::DescribeSurface(synced.rt,desc))
                        throw std::runtime_error("Textured quad target description failed");
                    gpu::TextureDesc tex{};
                    const bool has_texture=synced.textures[0]!=0;
                    if(has_texture&&!gpu::Describe(synced.textures[0],tex))throw std::runtime_error("Invalid capture texture");
                    const uint32_t capture_format=has_texture?tex.format:0xffffffffu;
                    const bool capture=live.detailed_draws||!live.diagnostic_texture_returns||(synced.vs&&!live.diagnostic_lit_returns)||
                        live.last_capture_format!=capture_format||live.last_capture_primitive!=Arg(cpu,0);
                    // Keep first-draw pixel proof and capture each new texture
                    // format/primitive. Normal repeated draws are read back at swap.
                    std::vector<uint8_t> before;
                    auto* pixels=gpu::CheckedGuestRange(desc.data,uint64_t(desc.pitch)*desc.height);
                    if(capture){
                        if(has_texture)std::fprintf(stderr,"D3D TEXTURE CAPTURE format=%02X width=%u height=%u pitch=%u levels=%u linear=%u\n",tex.format,tex.width,tex.height,tex.pitch,tex.levels,unsigned(tex.linear));
                        live.backend.ReadbackColor(synced.rt);
                        before.assign(pixels,pixels+size_t(desc.pitch)*desc.height);
                    }
                    live.bridge.ReplayTexturedDiffuseDiagnostic(Arg(cpu,0),start,count,indices);
                    unsigned changed=0;
                    if(capture){
                        live.backend.ReadbackColor(synced.rt);
                        for(unsigned y=0;y<desc.height;++y)for(unsigned x=0;x<desc.width;++x){
                            size_t at=size_t(y)*desc.pitch+4*x;
                            if(std::memcmp(before.data()+at,pixels+at,4))++changed;
                        }
                        live.last_capture_format=capture_format;live.last_capture_primitive=Arg(cpu,0);
                        if(!has_texture)std::fprintf(stderr,"D3D UNTEXTURED DIFFUSE DRAW PASS vertices=%u changed_pixels=%u\n",vertices,changed);
                        std::fprintf(stderr,"D3D ORIGINAL TEXTURED QUAD REPLAY: vertices=%u changed_pixels=%u (reduced state; not presented)\n",vertices,changed);
                        if(const char* path=std::getenv("HULK_REPLAY_BITMAP");path&&*path)
                            WriteDiagnosticBitmap(path,pixels,desc.width,desc.height,desc.pitch);
                    }else{
                        std::fprintf(stderr,"D3D TEXTURED DRAW PASS vertices=%u (pixel readback at swap)\n",vertices);
                    }
                    // b4edddb on RP6 produced recognizable copyright text
                    // (7335 changed pixels). Permit only this checked reduced
                    // profile, with opt-in and the existing frame budgets.
                    if(opted_in&&(!live.diagnostic_texture_returns?changed!=0:true)){
                        if(!live.diagnostic_texture_profile)
                            live.diagnostic_texture_profile=std::make_unique<gpu::State>(synced);
                        ++live.diagnostic_texture_returns;
                        if(synced.vs){++live.diagnostic_lit_returns;
                            if(live.diagnostic_lit_returns==1)std::fprintf(stderr,"D3D PACKED NORMAL LIGHT DRAW PASS vertices=%u changed_pixels=%u\n",vertices,changed);
                        }
                        ++live.diagnostic_draw_returns;
                        live.diagnostic_vertices+=vertices;
                        std::fprintf(stderr,"D3D DIAGNOSTIC TEXTURED DRAW RETURN: frame=%u batch=%u/%u total_textured=%u changed_pixels=%u measured=%u (reduced profile)\n",
                            live.diagnostic_swap_returns+1,frame_draws+1,live.diagnostic_draw_limit,
                            live.diagnostic_texture_returns,changed,unsigned(capture));
                        Ret(cpu,3,0);return;
                    }
                    throw std::runtime_error("D3D original textured quad replay completed; inspect bitmap");
                }
                const bool same_caller=caller==0x0031c10f&&!indexed&&Arg(cpu,0)==2&&
                    start==0&&count>0&&count<=1020&&count%2==0;
                const bool first=!live.diagnostic_line_profile;
                const bool matching=first?count==1020:
                    live.diagnostic_line_profile&&XbeD3d8Bridge::SameDiagnosticLineProfile(*live.diagnostic_line_profile,synced);
                // Preserve the original single diagnostic replay by default;
                // continuing multiple draws requires the explicit opt-in,
                // same observed raster/transform profile and bounded count.
                if(!indexed&&Arg(cpu,0)==2&&frame_draws<live.diagnostic_draw_limit&&
                    (first||(opted_in&&!zero_viewport&&same_caller&&matching))){
                        gpu::ScopedGuestMemory scope(mem_);
                        gpu::TextureDesc desc{};
                        if(!gpu::DescribeSurface(synced.rt,desc))
                            throw std::runtime_error("D3D diagnostic replay target description failed");
                        live.backend.ReadbackColor(synced.rt);
                        auto* pixels=gpu::CheckedGuestRange(desc.data,uint64_t(desc.pitch)*desc.height);
                        std::vector<uint8_t> before(pixels,pixels+size_t(desc.pitch)*desc.height);
                        live.bridge.ReplayFixedLinesDiagnostic(Arg(cpu,1),Arg(cpu,2));
                        live.backend.ReadbackColor(synced.rt);
                        unsigned changed=0;
                        for(unsigned y=0;y<desc.height;++y)for(unsigned x=0;x<desc.width;++x){
                            size_t at=size_t(y)*desc.pitch+4*x;
                            if(std::memcmp(before.data()+at,pixels+at,4))++changed;
                        }
                        std::fprintf(stderr,"D3D ORIGINAL LINE REPLAY (%s viewport %ux%u): changed_pixels=%u; reduced state, not a presented frame\n",
                            zero_viewport?"diagnostic target":"original guest",desc.width,desc.height,changed);
                        if(const char* path=std::getenv("HULK_REPLAY_BITMAP");path&&*path){
                            try{
                                WriteDiagnosticBitmap(path,pixels,desc.width,desc.height,desc.pitch);
                                std::fprintf(stderr,"D3D DIAGNOSTIC BITMAP: %s (reduced replay, not a presented frame)\n",path);
                            }catch(const std::exception& ex){std::fprintf(stderr,"D3D DIAGNOSTIC BITMAP FAILED: %s\n",ex.what());}
                        }
                    // The first draw must demonstrate a pixel change.
                    // Later valid draws may be occluded or overdraw identical
                    // pixels: a successful checked GPU call need not change
                    // the color buffer. The BMP is cumulative, not a frame.
                    if(opted_in&&!zero_viewport&&(!first||changed)&&same_caller&&matching&&
                        synced.viewport.w==desc.width&&synced.viewport.h==desc.height&&
                        frame_draws<live.diagnostic_draw_limit){
                        if(first)live.diagnostic_line_profile=std::make_unique<gpu::State>(synced);
                        ++live.diagnostic_draw_returns;live.diagnostic_vertices+=vertices;
                        std::fprintf(stderr,"D3D DIAGNOSTIC DRAW RETURN: frame=%u batch=%u/%u total_draws=%u vertices=%u cumulative_vertices=%u changed_pixels=%u (reduced profile)\n",
                            live.diagnostic_swap_returns+1,frame_draws+1,live.diagnostic_draw_limit,live.diagnostic_draw_returns,vertices,live.diagnostic_vertices,changed);
                        Ret(cpu,3,0);return;
                    }
                }
                if(!first)std::fprintf(stderr,"D3D DIAGNOSTIC BATCH STOP: completed=%u vertices=%u matching=%u same_caller=%u\n",
                    live.diagnostic_draw_returns,live.diagnostic_vertices,unsigned(matching),unsigned(same_caller));
                if(zero_viewport)
                    throw std::runtime_error("D3D original viewport has zero extent; investigate device initialization");
                throw std::runtime_error("D3D original geometry assembled; raw raster translation pending");
#else
                throw std::runtime_error("D3D draw requires GLES backend");
#endif
            }
            case kGraphicsVBlank: {
                Check(cpu,0);
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
                if(default_trace_||!live_d3d_||live_d3d_->owner!=cpu.thread_handle)
                    throw std::runtime_error("D3D vertical wait without owned live device");
                auto& live=*live_d3d_;
                const char* allow=std::getenv("HULK_CONTINUE_DRAW");
                if(!allow||std::strcmp(allow,"1")||!live.diagnostic_draw_returns||live.WaitLimitReached(true))
                    throw std::runtime_error("D3D vertical wait diagnostic continuation not enabled");
                DrainLivePush(live);
                SyncLiveTargets(live);
                live.backend.Flush();
                // Offscreen targets have no physical scanout. Pace on a
                // monotonic 60 Hz clock, not a fabricated hardware event.
                constexpr int64_t period_ns=16666667;
                auto now=Steady::now();
                auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
                auto next=now+std::chrono::nanoseconds(period_ns-elapsed%period_ns);
                lock.unlock();
                std::this_thread::sleep_until(next);
                lock.lock();
                Pause(cpu,lock);
                if(live.expanded_preview)sound_.Pump();
                ++live.diagnostic_vblanks;
                std::fprintf(stderr,"D3D LIVE VBLANK PASS caller=%08X (offscreen 60 Hz host pacing)\n",mem_.Read32(cpu.esp));
                Ret(cpu,0,0);return;
#else
                throw std::runtime_error("D3D vertical wait requires GLES backend");
#endif
            }
            case kGraphicsBlockOnTime: {
                Check(cpu,2);
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
                if(default_trace_||!live_d3d_||live_d3d_->owner!=cpu.thread_handle)
                    throw std::runtime_error("D3D wait without owned live device");
                auto& live=*live_d3d_;
                const auto dev=live.buffers.DeviceAddress();
                const auto target=Arg(cpu,0),flags=Arg(cpu,1);
                const auto current=mem_.Read32(dev+0x2c),callback=mem_.Read32(dev+0x19fc);
                std::fprintf(stderr,"D3D LIVE WAIT caller=%08X target=%08X current=%08X flags=%08X callback=%08X\n",
                    mem_.Read32(cpu.esp),target,current,flags,callback);
                const char* allow=std::getenv("HULK_CONTINUE_DRAW");
                if(!allow||std::strcmp(allow,"1")||!live.diagnostic_draw_returns||live.WaitLimitReached(false))
                    throw std::runtime_error("D3D initial wait diagnostic continuation not enabled");
                DrainLivePush(live);
                SyncLiveTargets(live);
                live.bridge.CompleteInitialGpuWait(target,current,flags,callback);
                ++live.diagnostic_gpu_waits;
                std::fprintf(stderr,"D3D LIVE WAIT PASS: GPU completed synchronous work, initial timeline only\n");
                Ret(cpu,2,0);return;
#else
                throw std::runtime_error("D3D wait requires GLES backend");
#endif
            }
            case kGraphicsSwap: {
                Check(cpu,1);
                std::fprintf(stderr,"D3D ORIGINAL SWAP caller=%08X flags=%08X\n",mem_.Read32(cpu.esp),Arg(cpu,0));
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
                if(default_trace_||!live_d3d_||live_d3d_->owner!=cpu.thread_handle)
                    throw std::runtime_error("D3D swap without owned live device");
                auto& live=*live_d3d_;
                const char* allow=std::getenv("HULK_CONTINUE_DRAW");
                const bool no_draw=live.diagnostic_draw_returns==live.draws_at_swap;
                const auto caller=mem_.Read32(cpu.esp);
                // RP6 f74de56: after 36 normal frames the original game
                // requested another swap with zero intervening draws.
                // Only continue the already verified two-buffer caller.
                if(!allow||std::strcmp(allow,"1")||!live.diagnostic_draw_returns||Arg(cpu,0)!=0||
                    (no_draw&&(!live.diagnostic_swap_returns||caller!=0x0031b1d4||live.buffers.BufferCount()!=2)))
                    throw std::runtime_error("Unsupported diagnostic swap request");
                if(!VulkanPreviewSelected())live.buffers.VerifyInitialTargets();
                DrainLivePush(live);
                SyncLiveTargets(live);
                gpu::ScopedGuestMemory scope(mem_);
                const auto state=live.bridge.Snapshot();
                gpu::TextureDesc desc{};
                if(!gpu::DescribeSurface(state.rt,desc)||state.rt.header!=live.buffers.BackBuffer(0))
                    throw std::runtime_error("Unsupported diagnostic swap target");
                if(no_draw){
                    live.backend.RestoreRotatedColor(state.rt);
                    std::fprintf(stderr,"D3D SWAP WITHOUT DRAW PASS: caller=%08X backing=%08X (preserved color)\n",caller,desc.data);
                }
                live.backend.ReadbackColor(state.rt);
                auto* pixels=gpu::CheckedGuestRange(desc.data,uint64_t(desc.pitch)*desc.height);
                if(preview_host.enabled.load())preview_host.Present(pixels,desc.width,desc.height,desc.pitch);
                if(live.expanded_preview)sound_.Pump();
                RefreshPreviewPorts();
                if(live.compact_capture){
                    live.last_presented_pixels.assign(pixels,pixels+size_t(desc.pitch)*desc.height);
                    live.last_presented_width=desc.width;live.last_presented_height=desc.height;live.last_presented_pitch=desc.pitch;
                    if(!live.diagnostic_swap_returns||(live.diagnostic_swap_returns+1)%120==0||
                        live.diagnostic_swap_returns+1>=live.frame_limit)live.SaveLastPresented();
                }else if(const char* path=std::getenv("HULK_REPLAY_BITMAP");path&&*path){
                    try{WriteDiagnosticBitmap(path,pixels,desc.width,desc.height,desc.pitch);}
                    catch(const std::exception& ex){std::fprintf(stderr,"D3D SWAP BITMAP FAILED: %s\n",ex.what());}
                }
                // Hash visible BGRA bytes, excluding pitch padding, so the
                // report distinguishes repeated frames from actual changes.
                uint64_t hash=14695981039346656037ull;
                for(unsigned y=0;y<desc.height;++y)for(unsigned x=0;x<desc.width*4;++x){
                    hash^=pixels[size_t(y)*desc.pitch+x];hash*=1099511628211ull;
                }
                const auto presented=live.bridge.Present(state.rt.header);
                if(presented>1&&hash==live.last_frame_hash)++live.identical_frames;
                live.last_frame_hash=hash;
                std::fprintf(stderr,"D3D ORIGINAL SWAP OFFSCREEN PASS: presents=%u draws=%u vertices=%u waits=%u (reduced diagnostic)\n",
                    presented,live.diagnostic_draw_returns,live.diagnostic_vertices,live.diagnostic_gpu_waits);
                std::fprintf(stderr,"D3D FRAME CHECKSUM frame=%u/%u hash=%016llX identical_to_previous_total=%u draws_in_frame=%u\n",
                    presented,live.frame_limit,static_cast<unsigned long long>(hash),live.identical_frames,live.diagnostic_draw_returns-live.draws_at_swap);
                const char* swap_opt=std::getenv("HULK_CONTINUE_SWAP");
                if(!swap_opt||std::strcmp(swap_opt,"1")||
                    presented!=live.diagnostic_swap_returns+1||mem_.Read32(cpu.esp)!=0x0031b1d4||live.buffers.BufferCount()!=2)
                    throw std::runtime_error("D3D original swap reached; guest continuation not enabled or outside verified swap profile");
                if(presented>=live.frame_limit)
                    throw std::runtime_error("D3D diagnostic frame budget reached (offscreen; no gameplay claim)");
                // Pinned XDK bridge semantics: stable surface headers,
                // physical data and lock words rotate, return swap count.
                const auto dev=live.buffers.DeviceAddress(),front=live.buffers.BackBuffer(-1),back=live.buffers.BackBuffer(0);
                const auto old_front=mem_.Read32(front+4),old_back=mem_.Read32(back+4);
                if(old_front==old_back||mem_.Read32(dev+0x2478)!=live.diagnostic_swap_returns||
                    mem_.Read32(front+8)||mem_.Read32(back+8)||
                    !physical_.Contains(0x80000000u+old_front,uint64_t(desc.pitch)*desc.height)||
                    !physical_.Contains(0x80000000u+old_back,uint64_t(desc.pitch)*desc.height))
                    throw std::runtime_error("Unsupported original swap backing state");
                const auto n=live.buffers.Rotate();
                if(n!=presented||mem_.Read32(front+4)!=old_back||mem_.Read32(back+4)!=old_front)
                    throw std::runtime_error("D3D swap rotation verification failed");
                if(VulkanPreviewSelected())live.bridge.SyncVulkanTargetMirrors(dev);
                else live.bridge.BindInitialTargets(back,live.buffers.Depth());
                live.backend.BeginRotatedFrame(live.bridge.Snapshot().rt);
                live.draws_at_swap=live.diagnostic_draw_returns;
                live.setters_at_swap=live.setter_calls;
                live.waits_at_swap=live.diagnostic_gpu_waits;
                live.vblanks_at_swap=live.diagnostic_vblanks;
                ++live.diagnostic_swap_returns;
                std::fprintf(stderr,"D3D ORIGINAL SWAP RETURN: count=%u front=%08X back=%08X (next frame requires full clear)\n",n,mem_.Read32(front+4),mem_.Read32(back+4));
                input_frame_=n;
                Ret(cpu,1,n);return;
#else
                throw std::runtime_error("D3D swap requires GLES backend");
#endif
            }
            case kGraphicsDraw: case kGraphicsDrawIndexed: {
                Check(cpu,3);
                std::fprintf(stderr,"D3D8 draw boundary id=%X caller=%08X first=%08X\n",ordinal,mem_.Read32(cpu.esp),Arg(cpu,0));
                throw std::runtime_error("D3D8 draw guest integration pending (generated tables unavailable)");
            }
            case 103: case 129: case 130: case 160: case 161: {
                Check(cpu,0);
                auto& level=Irql(cpu);const auto old=level;
                if(ordinal==103){Ret(cpu,0,old);return;}
                const auto requested=ordinal==129||ordinal==130?2u:(cpu.ecx&255u);
                if(requested>31||(ordinal==161?requested>old:requested<old))
                    throw std::runtime_error("Invalid guest IRQL transition");
                level=uint8_t(requested);
                Ret(cpu,0,ordinal==161?0:old);return;
            }
            case 358: Check(cpu,0);Ret(cpu,0,stopping_?1:0);return;
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
            case 67: Check(cpu,2);Ret(cpu,2,files_.CreateLink(Arg(cpu,0),Arg(cpu,1)));return;
            case 69: Check(cpu,1);Ret(cpu,1,files_.DeleteLink(Arg(cpu,0)));return;
            case 203: Check(cpu,2);Ret(cpu,2,files_.OpenLink(Arg(cpu,0),Arg(cpu,1)));return;
            case 215: Check(cpu,3);Ret(cpu,3,files_.QueryLink(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2)));return;
            case 190: case 202: {
                unsigned n=ordinal==190?9:6;Check(cpu,n);
                auto status=files_.Open(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),
                    ordinal==190?Arg(cpu,7):1,Arg(cpu,n-1));
                std::fprintf(stderr,"DISC OPEN CALL ordinal=%u caller=%08X access=%08X attrs=%08X disposition=%u options=%08X status=%08X\n",
                    ordinal,mem_.Read32(cpu.esp),Arg(cpu,1),Arg(cpu,2),ordinal==190?Arg(cpu,7):1,Arg(cpu,n-1),status);
                Ret(cpu,n,status);return;
            }
            case 207: {
                Check(cpu,10);
                if(Arg(cpu,2))throw std::runtime_error("Directory APC not implemented");
                auto event=Arg(cpu,1);
                if(event&&(!events_.count(event)||semaphore_limits_.count(events_.at(event).body)||timer_types_.count(events_.at(event).body)||mutants_.count(events_.at(event).body))){
                    Ret(cpu,10,0xc0000008u);return;
                }
                auto status=files_.QueryDirectory(Arg(cpu,0),Arg(cpu,4),Arg(cpu,5),Arg(cpu,6),Arg(cpu,7),Arg(cpu,8),(Arg(cpu,9)&255u)!=0);
                if(event){mem_.Write32(events_.at(event).body+4,1);cv_.notify_all();}
                std::fprintf(stderr,"DISC DIRECTORY caller=%08X handle=%08X class=%u status=%08X\n",
                    mem_.Read32(cpu.esp),Arg(cpu,0),Arg(cpu,7),status);
                Ret(cpu,10,status);return;
            }
            case 218: {
                Check(cpu,5);
                auto status=files_.QueryVolume(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4));
                std::fprintf(stderr,"XBOX VOLUME QUERY caller=%08X handle=%08X length=%u class=%u status=%08X\n",
                    mem_.Read32(cpu.esp),Arg(cpu,0),Arg(cpu,3),Arg(cpu,4),status);
                Ret(cpu,5,status);return;
            }
            // Same-title Windows runtime identifies these ordinals.
            // Use the existing cooperative lock/deadline scheduler, not
            // its Windows object table. No fabricated I/O or APC delivery.
            case 191: {
                Check(cpu,4);auto out=Arg(cpu,0);
                if(!out||!mem_.IsCommitted(out,4)||Arg(cpu,2)){
                    Ret(cpu,4,0xc000000du);return;
                }
                if(completions_.size()>=256||next_completion_>=0x60010000){
                    Ret(cpu,4,0xc000011fu);return;
                }
                auto h=next_completion_;next_completion_+=4;completions_.emplace(h,std::make_shared<XbeIoCompletion>());
                Output(out,h);Ret(cpu,4,0);return;
            }
            case 227: {
                Check(cpu,5);auto it=completions_.find(Arg(cpu,0));
                if(it==completions_.end()){Ret(cpu,5,0xc0000008u);return;}
                if(it->second->Full()){Ret(cpu,5,0xc000009au);return;}
                it->second->packets.push_back({Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4)});
                cv_.notify_all();Ret(cpu,5,0);return;
            }
            case 223: {
                Check(cpu,5);auto it=completions_.find(Arg(cpu,0));
                if(it==completions_.end()){Ret(cpu,5,0xc0000008u);return;}
                auto port=it->second;
                auto key=Arg(cpu,1),context=Arg(cpu,2),iosb=Arg(cpu,3),timeout=Arg(cpu,4);
                if(!key||!context||!iosb||!mem_.IsCommitted(key,4)||!mem_.IsCommitted(context,4)||
                   !mem_.IsCommitted(iosb,8)||(timeout&&!mem_.IsCommitted(timeout,8))){
                    Ret(cpu,5,0xc000000du);return;
                }
                auto deadline=Deadline(timeout);
                while(port->packets.empty()){
                    Pause(cpu,lock);ServiceDpc(cpu,lock);
                    if(!port->packets.empty())break;
                    if(Steady::now()>=deadline){Ret(cpu,5,0x102);return;}
                    if(!dpc_running_&&!dpc_queue_.empty()&&Irql(cpu)<2)continue;
                    cv_.wait_until(lock,NextTimer(deadline));
                }
                // Guest memory could change while waiting.
                if(!mem_.IsCommitted(key,4)||!mem_.IsCommitted(context,4)||!mem_.IsCommitted(iosb,8)){
                    Ret(cpu,5,0xc000000du);return;
                }
                auto packet=port->packets.front();port->packets.pop_front();
                Output(key,packet.key);Output(context,packet.context);
                Output(iosb,packet.status);Output(iosb+4,packet.information);
                Ret(cpu,5,0);return;
            }
            case 219: {
                Check(cpu,8);
                if(Arg(cpu,2)) throw std::runtime_error("File APC not implemented");
                auto event=Arg(cpu,1);
                if(event && (!events_.count(event)||semaphore_limits_.count(events_.at(event).body)||timer_types_.count(events_.at(event).body)||mutants_.count(events_.at(event).body))){Ret(cpu,8,0xc0000008u);return;}
                uint32_t completion_key=0;
                auto port=files_.Completion(Arg(cpu,0),completion_key);
                auto context=Arg(cpu,3),iosb=Arg(cpu,4);
                if(port&&context){
                    if(port->Full()){Ret(cpu,8,0xc000009au);return;}
                    if(!mem_.IsCommitted(iosb,8)||!mem_.IsCommitted(Arg(cpu,5),Arg(cpu,6))||
                        Arg(cpu,6)>64u*1024*1024||(Arg(cpu,7)&&!mem_.IsCommitted(Arg(cpu,7),8))){Ret(cpu,8,0xc000000du);return;}
                }
                auto status=files_.Read(Arg(cpu,0),iosb,Arg(cpu,5),Arg(cpu,6),Arg(cpu,7));
                if(port&&context){
                    port->packets.push_back({completion_key,context,status,mem_.Read32(iosb+4)});
                    cv_.notify_all();
                }
                if(event){mem_.Write32(events_.at(event).body+4,1);cv_.notify_all();}
                std::fprintf(stderr,"DISC READ caller=%08X handle=%08X requested=%u status=%08X\n",
                    mem_.Read32(cpu.esp),Arg(cpu,0),Arg(cpu,6),status);
                Ret(cpu,8,status);return;
            }
            case 236: {
                Check(cpu,8);
                if(Arg(cpu,2))throw std::runtime_error("Metadata write APC not implemented");
                auto event=Arg(cpu,1);
                if(event&&(!events_.count(event)||semaphore_limits_.count(events_.at(event).body)||timer_types_.count(events_.at(event).body)||mutants_.count(events_.at(event).body))){
                    Ret(cpu,8,0xc0000008u);return;
                }
                auto status=files_.WriteMetadata(Arg(cpu,0),Arg(cpu,4),Arg(cpu,5),Arg(cpu,6),Arg(cpu,7));
                if(event){mem_.Write32(events_.at(event).body+4,1);cv_.notify_all();}
                std::fprintf(stderr,"XBOX WRITE caller=%08X handle=%08X bytes=%u status=%08X\n",
                    mem_.Read32(cpu.esp),Arg(cpu,0),Arg(cpu,6),status);
                Ret(cpu,8,status);return;
            }
            case 198: Check(cpu,2);Ret(cpu,2,files_.Flush(Arg(cpu,0),Arg(cpu,1)));return;
            case 210: Check(cpu,2);Ret(cpu,2,files_.FullAttributes(Arg(cpu,0),Arg(cpu,1)));return;
            case 211: Check(cpu,5);Ret(cpu,5,files_.Query(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4)));return;
            case 226: {
                Check(cpu,5);
                if(Arg(cpu,4)==30){
                    auto in=Arg(cpu,2);
                    if(Arg(cpu,3)<8||!mem_.IsCommitted(in,8)||!mem_.IsCommitted(Arg(cpu,1),8)){
                        Ret(cpu,5,0xc000000du);return;
                    }
                    auto port=completions_.find(mem_.Read32(in));
                    if(port==completions_.end()){Ret(cpu,5,0xc0000008u);return;}
                    Ret(cpu,5,files_.AssociateCompletion(Arg(cpu,0),Arg(cpu,1),port->second,mem_.Read32(in+4)));return;
                }
                Ret(cpu,5,files_.Set(Arg(cpu,0),Arg(cpu,1),Arg(cpu,2),Arg(cpu,3),Arg(cpu,4)));return;
            }
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
                    virtual_.Allocate(b,z,Arg(cpu,3),Arg(cpu,4),Arg(cpu,1)):
                    virtual_.Free(b,z,Arg(cpu,2));
                if(ordinal==184){
                    std::fprintf(stderr,"VM ALLOC caller=%08X base=%08X size=%08X zero=%08X type=%08X protect=%08X status=%08X result_base=%08X result_size=%08X\n",
                        mem_.Read32(cpu.esp),mem_.Read32(bp),mem_.Read32(sp),Arg(cpu,1),Arg(cpu,3),Arg(cpu,4),status,b,z);
                }
                if(!status){mem_.Write32(bp,b);mem_.Write32(sp,z);}
                Ret(cpu,n,status);return;
            }
            case 217: Check(cpu,2);Ret(cpu,2,virtual_.Query(Arg(cpu,0),Arg(cpu,1)));return;
            case 165: case 166: {
                unsigned n=ordinal==165?1:5;Check(cpu,n);
                auto bytes=Arg(cpu,0),lo=ordinal==165?0:Arg(cpu,1),
                    hi=ordinal==165?0xffffffffu:Arg(cpu,2),align=ordinal==165?0:Arg(cpu,3),
                    protect=ordinal==165?4:Arg(cpu,4);
                std::fprintf(stderr,"MM CONTIG REQUEST caller=%08X bytes=%08X low=%08X high=%08X align=%08X protect=%08X\n",
                    mem_.Read32(cpu.esp),bytes,lo,hi,align,protect);
                if(!XbePhysicalMemory::SupportedProtection(protect))
                    throw std::runtime_error("Unsupported physical memory protection/alias");
                auto a=physical_.Allocate(bytes,lo,hi,align,protect);
                std::fprintf(stderr,"MM CONTIG RESULT base=%08X physical=%08X\n",a,physical_.Physical(a));
                Ret(cpu,n,a);return;
            }
            case 178: {
                Check(cpu,3);auto a=Arg(cpu,0),bytes=Arg(cpu,1);bool persist=(Arg(cpu,2)&255u)!=0;
                std::fprintf(stderr,"MmPersistContiguousMemory base=%08X bytes=%08X persist=%u\n",a,bytes,unsigned(persist));
                if(!physical_.Persist(a,bytes,persist))
                    throw std::runtime_error("Persistence outside committed contiguous Xbox RAM");
                // The session owns these flags; a firmware quick reboot is
                // still unsupported. Explicit frees clear the flags.
                Ret(cpu,3,0);return;
            }
            case 169: {
                Check(cpu,2);auto bytes=Arg(cpu,0);
                if(Arg(cpu,1)&255u)throw std::runtime_error("Debugger kernel stack not supported");
                auto top=stacks_.Allocate(bytes);
                std::fprintf(stderr,"MmCreateKernelStack bytes=%08X top=%08X\n",bytes,top);
                Ret(cpu,2,top);return;
            }
            case 170: {
                Check(cpu,2);auto top=Arg(cpu,0),limit=Arg(cpu,1);
                if(!stacks_.Free(top,limit,cpu.esp))
                    throw std::runtime_error("Invalid or active kernel stack deletion");
                Ret(cpu,2,0);return;
            }
            case 167: {
                Check(cpu,2);auto bytes=Arg(cpu,0),protect=Arg(cpu,1);
                if(protect!=4)throw std::runtime_error("Unsupported system memory protection");
                uint64_t rounded=(uint64_t(bytes)+4095)&~4095ull;
                if(!rounded||rounded>0x01000000){Ret(cpu,2,0);return;}
                auto a=pool_.Allocate(uint32_t(rounded),4096);
                if(a)system_allocations_.emplace(a,uint32_t(rounded));
                Ret(cpu,2,a);return;
            }
            case 172: {
                Check(cpu,2);auto a=Arg(cpu,0),bytes=Arg(cpu,1);
                auto it=system_allocations_.find(a);
                uint64_t rounded=(uint64_t(bytes)+4095)&~4095ull;
                if(it==system_allocations_.end()||rounded!=it->second)
                    throw std::runtime_error("Unsupported partial or invalid system memory free");
                auto pages=it->second/4096;pool_.Free(a);system_allocations_.erase(it);
                Ret(cpu,2,pages);return;
            }
            case 174: case 179: {
                Check(cpu,1);bool mapped=mem_.IsCommitted(Arg(cpu,0),1);
                auto protect=physical_.Protection(Arg(cpu,0));
                Ret(cpu,1,ordinal==174?uint32_t(mapped):mapped?(protect?protect:4u):0u);return;
            }
            case 175: {
                Check(cpu,3);auto a=Arg(cpu,0),bytes=Arg(cpu,1);
                // Diagnostic memory is non-relocating while committed.
                // This is not a host DMA registration or physical page alias.
                if(bytes&&!mem_.IsCommitted(a,bytes))throw std::runtime_error("Lock of uncommitted guest buffer");
                Ret(cpu,3,0);return;
            }
            case 176: {
                Check(cpu,2);auto a=Arg(cpu,0);
                if(a<0x04000000u)a|=kXboxPhysicalBase;
                if(!physical_.Contains(a,1))throw std::runtime_error("Lock of unmapped Xbox physical page");
                Ret(cpu,2,0);return;
            }
            case 23: Check(cpu,1);Ret(cpu,1,pool_.AllocationSize(Arg(cpu,0)));return;
            case 171: Check(cpu,1);
                if(!physical_.Free(Arg(cpu,0))) throw std::runtime_error("Invalid contiguous free");
                Ret(cpu,1,0);return;
            case 173: {
                Check(cpu,1);auto addr=Arg(cpu,0);
                if(!physical_.Contains(addr,1)) throw std::runtime_error("No physical mapping for guest address");
                Ret(cpu,1,physical_.Physical(addr));return;
            }
            case 181: {
                Check(cpu,1);auto out=Arg(cpu,0);
                if(!out||!mem_.IsCommitted(out,36)||mem_.Read32(out)!=36){
                    Ret(cpu,1,0xc000000du);return;
                }
                constexpr uint32_t total=0x04000000/4096;
                auto committed=virtual_.CommittedBytes(),reserved=virtual_.ReservedBytes();
                auto pool_pages=pool_.UsedPages();
                uint32_t stacks=initial_stack_pages_+stacks_.Pages();
                for(size_t i=0;i<count_;++i)stacks+=workers_[i].stack_pages;
                // Diagnostic allocations use separate host arenas; account
                // for live Xbox-equivalent pages, not Android total RAM.
                uint64_t used=uint64_t(kXboxKernelReservedBytes/4096)+physical_.AllocatedPages()+
                    committed/4096+pool_pages+stacks+image_pages_;
                uint32_t available=used>=total?0:total-uint32_t(used);
                const std::array<uint32_t,9> stats={36,total,available,committed,reserved,
                    0,pool_pages,stacks,image_pages_};
                mem_.Write(out,stats.data(),sizeof(stats));
                std::fprintf(stderr,"MmQueryStatistics total=%u available=%u virtual=%u reserved=%u pool=%u stack=%u image=%u\n",
                    total,available,committed,reserved,pool_pages,stacks,image_pages_);
                Ret(cpu,1,0);return;
            }
            case 180: {
                Check(cpu,1);auto addr=Arg(cpu,0);
                auto size=physical_.Size(addr);
                if(!size)size=pool_.AllocationSize(addr);
                Ret(cpu,1,size?size:virtual_.AllocationSize(addr));return;
            }
            case 327: case 328: {
                Check(cpu,1);
                if(!sections_)throw std::runtime_error("Xe section registry unavailable");
                Ret(cpu,1,sections_->Change(Arg(cpu,0),ordinal==327));return;
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
                    if(!dpc_running_&&!dpc_queue_.empty()&&Irql(cpu)<2)continue;
                    cv_.wait_until(lock,NextTimer(deadline));
                }while(true);
                Pause(cpu,lock); Ret(cpu,3,0); return;
            }
            case 194: {
                Check(cpu,3);auto out=Arg(cpu,0),type=Arg(cpu,2);
                if(!mem_.IsCommitted(out,4)||type>1){Ret(cpu,3,0xc000000du);return;}
                if(Arg(cpu,1))throw std::runtime_error("Named timer not implemented");
                if(timer_types_.size()>=256)throw std::runtime_error("Timer capacity exhausted");
                auto body=pool_.Allocate(40);
                if(!body){Ret(cpu,3,0xc0000017u);return;}
                InitDispatcher(body,40,8+type,0);timer_types_[body]=type;
                auto h=next_event_;next_event_+=4;
                events_.emplace(h,Event{body,type==0});Output(out,h);
                Ret(cpu,3,0);return;
            }
            case 185: case 229: {
                const unsigned n=ordinal==185?2:8;Check(cpu,n);
                auto body=Body(Arg(cpu,0));
                if(!body){Ret(cpu,n,0xc0000008u);return;}
                if(!timer_types_.count(body)){Ret(cpu,n,0xc0000024u);return;}
                auto out=Arg(cpu,n-1);
                if(out&&!mem_.IsCommitted(out,1)){Ret(cpu,n,0xc000000du);return;}
                ServiceTimers();
                uint8_t prev=mem_.Read32(body+4)!=0;
                if(ordinal==185){
                    timers_.erase(body);mem_.Write32(body,mem_.Read32(body)&~0x01000000u);
                }else{
                    if(Arg(cpu,2)||Arg(cpu,5))throw std::runtime_error("Timer APC or resume semantics not implemented");
                    auto period=Arg(cpu,6);auto ptr=Arg(cpu,1);
                    if(period>86400000||!mem_.IsCommitted(ptr,8)){Ret(cpu,n,0xc000000du);return;}
                    int64_t ticks;mem_.Read(ptr,&ticks,8);auto now=Now100ns();
                    uint64_t delta=ticks<0?uint64_t(-(ticks+1))+1:ticks>now?uint64_t(ticks-now):0;
                    if(delta>864000000000ull){Ret(cpu,n,0xc000000du);return;}
                    timers_[body]={Steady::now()+std::chrono::nanoseconds(delta*100),0,period};
                    mem_.Write32(body,mem_.Read32(body)|0x01000000u);mem_.Write32(body+4,0);
                    mem_.Write(body+16,&ticks,8);mem_.Write32(body+32,0);mem_.Write32(body+36,period);
                }
                if(out)mem_.Write(out,&prev,1);
                cv_.notify_all();Ret(cpu,n,0);return;
            }
            case 197: {
                Check(cpu,3);auto h=Arg(cpu,0),out=Arg(cpu,1),options=Arg(cpu,2);
                if(!mem_.IsCommitted(out,4)||options&~1u){Ret(cpu,3,0xc000000du);return;}
                if(files_.Contains(h)){Ret(cpu,3,files_.Duplicate(h,out,options&1));return;}
                auto it=events_.find(h);
                if(it==events_.end())throw std::runtime_error("DuplicateObject currently supports dispatcher and disc handles only");
                auto copy=it->second;auto newh=next_event_;next_event_+=4;
                events_.emplace(newh,copy);Output(out,newh);
                if(options&1)events_.erase(h);
                Ret(cpu,3,0);return;
            }
            case 230: {
                Check(cpu,5);
                if(Arg(cpu,3)&255u)throw std::runtime_error("Alertable signal-and-wait not implemented");
                auto signal=Body(Arg(cpu,0)),wait=Body(Arg(cpu,1));
                if(!signal||!wait){Ret(cpu,5,0xc0000008u);return;}
                auto type=mem_.Read32(signal)&255u;
                if(type==0||type==1)mem_.Write32(signal+4,1);
                else if(type==5){
                    auto old=mem_.Read32(signal+4);
                    if(!semaphore_limits_.count(signal)||old>=semaphore_limits_.at(signal)){Ret(cpu,5,0xc0000047u);return;}
                    mem_.Write32(signal+4,old+1);
                }else if(type==2){
                    auto status=ReleaseMutant(signal,cpu.thread_handle,0);
                    if(status){Ret(cpu,5,status);return;}
                }else{Ret(cpu,5,0xc0000024u);return;}
                cv_.notify_all();
                auto result=WaitBody(cpu,wait,Arg(cpu,4),lock);Ret(cpu,5,result);return;
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
            case 192: {
                Check(cpu,3);auto out=Arg(cpu,0),attrs=Arg(cpu,1),owner=Arg(cpu,2)&255u;
                if(!out||!mem_.IsCommitted(out,4)){Ret(cpu,3,0xc000000du);return;}
                if(attrs){
                    if(!mem_.IsCommitted(attrs,12)){Ret(cpu,3,0xc000000du);return;}
                    if(mem_.Read32(attrs)||mem_.Read32(attrs+4)||mem_.Read32(attrs+8))
                        throw std::runtime_error("Named/attributed mutant not implemented");
                }
                if(mutants_.size()>=1024){Ret(cpu,3,0xc0000017u);return;}
                auto body=pool_.Allocate(32);
                if(!body){Ret(cpu,3,0xc0000017u);return;}
                InitDispatcher(body,32,2,1);
                mem_.Write32(body+16,body+16);mem_.Write32(body+20,body+16);
                Mutant m{owner?cpu.thread_handle:0u,owner?1u:0u,false};
                mutants_.emplace(body,m);SyncMutant(body,m);
                auto h=next_event_;next_event_+=4;
                events_.emplace(h,Event{body,false});Output(out,h);
                std::fprintf(stderr,"HULK MUTANT CREATE PASS handle=%08X body=%08X owner=%08X depth=%u\n",h,body,m.owner,m.depth);
                Ret(cpu,3,0);return;
            }
            case 221: {
                Check(cpu,2);auto h=Arg(cpu,0),body=Body(h);
                Ret(cpu,2,body?ReleaseMutant(body,cpu.thread_handle,Arg(cpu,1)):0xc0000008u);return;
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
                    auto signal=[&](uint32_t b){return Ready(b,cpu.thread_handle);};
                    return kind?std::any_of(bodies.begin(),bodies.end(),signal):
                                std::all_of(bodies.begin(),bodies.end(),signal);
                };
                do {
                    Pause(cpu,lock);ServiceDpc(cpu,lock);
                    if(ready())break;
                    if(Steady::now()>=deadline){Ret(cpu,6,0x102);return;}
                    if(!dpc_running_&&!dpc_queue_.empty()&&Irql(cpu)<2)continue;
                    cv_.wait_until(lock,NextTimer(deadline));
                }while(!ready());
                if(!kind){
                    uint32_t result=0;
                    for(uint32_t i=0;i<n;++i)if(Consume(bodies[i],cpu.thread_handle)&&!result)result=0x80+i;
                    Ret(cpu,6,result);return;
                }
                for(uint32_t i=0;i<n;++i)if(Ready(bodies[i],cpu.thread_handle)){
                    Ret(cpu,6,i+Consume(bodies[i],cpu.thread_handle));return;
                }
                throw std::runtime_error("Multiwait lost signal");
            }
            case 225: case 186: {
                const unsigned n=ordinal==225?2:1;Check(cpu,n);
                auto it=events_.find(Arg(cpu,0));
                if(it==events_.end()||semaphore_limits_.count(it->second.body)||timer_types_.count(it->second.body)||mutants_.count(it->second.body)){Ret(cpu,n,0xc0000008u);return;}
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
                if(completions_.erase(h)){Ret(cpu,1,0);return;}
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
                    if(events_.count(h)){
                        ordinal_type=mutants_.count(body)?22:timer_types_.count(body)?31:semaphore_limits_.count(body)?30:16;
                    }
                    const auto actual=registry_?registry_->Resolve(ordinal_type).guest_data:0;
                    if(!actual || actual!=requested){Ret(cpu,3,0xc0000024u);return;}
                }
                auto out=Arg(cpu,2);
                if(!out||!mem_.IsCommitted(out,4)){Ret(cpu,3,0xc000000du);return;}
                auto& refs=object_references_[body];
                if(refs==UINT32_MAX)throw std::runtime_error("Object reference count overflow");
                ++refs;Output(out,body);Ret(cpu,3,0);return;
            }
            case 250: case 251: {
                // Xbox FASTCALL: object pointer in ECX; only return address on stack.
                Check(cpu,0);auto body=cpu.ecx;
                if(ordinal==251){
                    if(!PointerObject(body))throw std::runtime_error("Reference to unknown guest object");
                    auto& refs=object_references_[body];
                    if(refs==UINT32_MAX)throw std::runtime_error("Object reference count overflow");
                    ++refs;
                }else{
                    auto it=object_references_.find(body);
                    if(it==object_references_.end()||!it->second)
                        throw std::runtime_error("Dereference without a matching guest object reference");
                    if(!--it->second)object_references_.erase(it);
                }
                Ret(cpu,0,0);return;
            }
            case 124: case 143: case 144: {
                unsigned n=ordinal==124?1:2;Check(cpu,n);auto body=Arg(cpu,0);
                auto it=thread_settings_.find(body);
                if(it==thread_settings_.end()||!mem_.IsCommitted(body,16))
                    throw std::runtime_error("Unknown KTHREAD for priority/boost request");
                auto& state=it->second;
                uint32_t previous=uint32_t(state.priority);
                if(ordinal==143){
                    uint32_t raw=Arg(cpu,1);std::memcpy(&state.priority,&raw,sizeof(raw));
                    std::fprintf(stderr,"KeSetBasePriorityThread body=%08X old=%d new=%d\n",body,int(state.priority),int(previous));
                }else if(ordinal==144){
                    previous=state.disable_boost;
                    state.disable_boost=(Arg(cpu,1)&255u)!=0;
                }
                // Guest scheduling metadata only; no Android thread priority
                // elevation. Match pinned XboxXBERecomp's old-priority return.
                Ret(cpu,n,previous);return;
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
            case 238: Check(cpu,0); Ret(cpu,0,0); lock.unlock(); std::this_thread::yield(); lock.lock(); return;
            case 258: Check(cpu,1); throw GuestThreadExit{Arg(cpu,0)};
            default: break;
        }
        if(!DispatchPoolCall(ordinal,cpu,mem_,pool_)){
            DumpRecent();
            throw std::runtime_error("Unsupported kernel ordinal "+std::to_string(ordinal));
        }
    }
    void Join() {
        for(size_t i=0;;++i) {
            std::thread worker;
            { std::lock_guard<std::mutex> lock(lock_);
              if(i>=count_) break;
              worker=std::move(workers_[i].host); }
            if(worker.joinable()){GuestJoinSignalMask mask;worker.join();}
        }
    }
    void Stop() {
        { std::lock_guard<std::mutex> lock(lock_); stopping_=true; cv_.notify_all(); }
        Join();
    }
public:
    XbeKernelSession(XboxGuestAddressSpace& mem,uint32_t base,uint32_t size,
                     XbeTlsTemplate tls={},const HulkKernelClock* clock=nullptr,const std::string& disc_root={},const HulkKernelDataRegistry* registry=nullptr,uint32_t image_pages=0,const std::string& partition1={},XbeResidentSections* sections=nullptr)
        :mem_(mem),pool_(mem,base,size),sound_(mem,pool_),virtual_(mem),physical_(mem),stacks_(mem),files_(mem,disc_root,partition1),tls_(tls),clock_(clock),registry_(registry),sections_(sections),image_pages_(image_pages) {}
    XbeKernelSession(const XbeKernelSession&)=delete;
    XbeKernelSession& operator=(const XbeKernelSession&)=delete;
    ~XbeKernelSession() { Stop(); }
    static void Dispatch(X86Context& cpu,uint8_t* base,uint32_t ordinal) {
        if(!active_ || active_->mem_.Base()!=base)
            throw std::runtime_error("Invalid kernel session");
        // Fiber memory accesses are synchronous checked guest operations,
        // not guest kernel calls. Do not run DPCs or alter the fault trace.
        if(ordinal==kFiberRead32||ordinal==kFiberWrite32){
            std::lock_guard<std::mutex> guard(active_->lock_);
            auto& mem=active_->mem_;
            if(!mem.IsCommitted(cpu.ecx,4))
                throw std::runtime_error("Fiber access outside committed guest memory");
            if(ordinal==kFiberRead32)cpu.eax=mem.Read32(cpu.ecx);
            else mem.Write32(cpu.ecx,cpu.eax);
            return;
        }
        fault_thread=static_cast<sig_atomic_t>(cpu.thread_handle);
        fault_ordinal=static_cast<sig_atomic_t>(ordinal);
        fault_stack=static_cast<sig_atomic_t>(cpu.esp);
        fault_return=0;
        std::unique_lock<std::mutex> lock(active_->lock_);
        if(active_->mem_.IsCommitted(cpu.esp,4)) fault_return=static_cast<sig_atomic_t>(active_->mem_.Read32(cpu.esp));
        auto& session=*active_;
        // Pure observation: do not pause, service DPCs, consume stack args
        // or touch the original CPU context while a guest routine is active.
        if(ordinal==kLightingMaterialTrace||ordinal==kLightingLightTrace||ordinal==kLightingEnableTrace){
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
            auto* live=session.live_d3d_.get();
            if(live&&live->owner==cpu.thread_handle){
                auto& mem=session.mem_;
                auto n=ordinal==kLightingMaterialTrace?1u:2u;
                if(mem.IsCommitted(cpu.esp,4+4*n)){
                    auto a=mem.Read32(cpu.esp+4),b=n==2?mem.Read32(cpu.esp+8):0u;
                    bool ok=ordinal==kLightingMaterialTrace?live->bridge.ObserveMaterial(a):
                        ordinal==kLightingLightTrace?live->bridge.ObserveLight(a,b):live->bridge.ObserveLightEnable(a,b);
                    std::fprintf(stderr,"D3D LIGHT OBSERVE id=%08X a=%08X b=%08X accepted=%u\n",ordinal,a,b,unsigned(ok));
                }
            }
#endif
            return;
        }
        if(ordinal==kStartupTextTrace){
            auto& mem=session.mem_;
            // The verified call at 001BB339 pushes flag 0, then the
            // UTF-16 line pointer. Do not infer an ABI at other callsites.
            if(session.startup_text_lines_<32&&mem.IsCommitted(cpu.esp,12)&&
               mem.Read32(cpu.esp)==0x001bb33e&&mem.Read32(cpu.esp+8)==0){
                const auto p=mem.Read32(cpu.esp+4);
                std::string line;
                bool terminated=false;
                for(unsigned i=0;i<512;++i){
                    const uint64_t a=uint64_t(p)+2*i;
                    if(!p||p%2||a+2>XboxGuestAddressSpace::kSize||!mem.IsCommitted(uint32_t(a),2))break;
                    uint16_t ch=0;mem.Read(uint32_t(a),&ch,2);
                    if(!ch){terminated=true;break;}
                    if(ch>=32&&ch<=126)line.push_back(char(ch));
                    else {char escaped[8];std::snprintf(escaped,sizeof(escaped),"\\u%04X",unsigned(ch));line+=escaped;}
                }
                ++session.startup_text_lines_;
                std::fprintf(stderr,"HULK STARTUP TEXT caller=001BB33E ptr=%08X complete=%u text=%s\n",
                    p,unsigned(terminated),line.c_str());
            }
            return;
        }
        if(ordinal==kViewportTraceEnter||ordinal==kViewportTraceLeave){
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
            auto* live=session.live_d3d_.get();
            if(live&&live->owner==cpu.thread_handle){
                auto& mem=session.mem_;
                auto dev=live->buffers.DeviceAddress();
                bool before=ordinal==kViewportTraceEnter;
                if(live->expanded_preview&&live->compact_capture&&!live->detailed_draws){
                    if(before)live->viewport_trace_sampled=DiagnosticTracePoll(++live->viewport_trace_enters);
                    if(!live->viewport_trace_sampled)return;
                }
                std::fprintf(stderr,"D3D VIEWPORT WRITER %s caller=%08X esp=%08X eax=%08X ecx=%08X edx=%08X fields=",
                    before?"ENTER":"LEAVE",mem.IsCommitted(cpu.esp,4)?mem.Read32(cpu.esp):0,
                    cpu.esp,cpu.eax,cpu.ecx,cpu.edx);
                for(unsigned i=0;i<6;++i)std::fprintf(stderr," %08X",mem.Read32(dev+0xee0+4*i));
                std::fprintf(stderr,"\n");
                if(before&&mem.IsCommitted(cpu.esp,20)){
                    for(unsigned i=0;i<4;++i){
                        auto v=mem.Read32(cpu.esp+4+4*i);
                        std::fprintf(stderr,"D3D VIEWPORT WRITER stack_candidate %u=%08X\n",i,v);
                        if(v>=0x10000&&v%4==0&&mem.IsCommitted(v,24)){
                            std::fprintf(stderr,"D3D VIEWPORT WRITER candidate %u words:",i);
                            for(unsigned j=0;j<6;++j)std::fprintf(stderr," %08X",mem.Read32(v+4*j));
                            std::fprintf(stderr,"\n");
                        }
                    }
                    for(auto off:{0x930u,0x934u,0x938u,0x93cu,0x940u,0x944u,0x948u,0x94cu,0x950u,0x954u,0x958u,0x95cu,0x960u,0x964u,0x968u,0x96cu,0x970u,0x974u,0x978u,0x97cu,0x980u,0x1a00u,0x1a04u,0x1a08u,0x1a0cu,0x1a10u,0x1a14u,0x1a18u,0x1a1cu,0x1a20u})
                        std::fprintf(stderr,"D3D VIEWPORT WRITER DEVICE +%04X=%08X\n",off,mem.Read32(dev+off));
                }
            }
#endif
            return;
        }
        const auto seq=session.trace_seq_++;
        auto& trace=session.recent_[seq%session.recent_.size()];
        trace={seq,ordinal,cpu.thread_handle,uint32_t(fault_return),cpu.esp,cpu.eax,false};
        session.Call(cpu,ordinal,lock);
        if(trace.seq==seq){trace.eax=cpu.eax;trace.returned=true;}
        session.ServiceDpc(cpu,lock);
    }
    int Run(Entry entry,X86Context& cpu,uint32_t address) {
        if(!entry || active_ || entry_) throw std::runtime_error("Invalid kernel session");
        struct Reset { XbeKernelSession& s; ~Reset(){s.entry_=nullptr; active_=nullptr;} } reset{*this};
        HulkKernelClockPump clock_pump(mem_,clock_);
        stopping_=false; worker_failure_=0;
        if(cpu.fs_base&&mem_.IsCommitted(cpu.fs_base+0x28,4)){
            auto body=mem_.Read32(cpu.fs_base+0x28);
            if(body&&mem_.IsCommitted(body,16))thread_settings_.emplace(body,ThreadSettings{});
        }
        if(cpu.fs_base&&mem_.IsCommitted(cpu.fs_base,12)){
            auto top=mem_.Read32(cpu.fs_base+4),bottom=mem_.Read32(cpu.fs_base+8);
            if(top>bottom&&mem_.IsCommitted(bottom,top-bottom))
                initial_stack_pages_=(top-bottom+4095)/4096;
        }
        GuestFaultStack fault_stack_scope;
        fault_thread=static_cast<sig_atomic_t>(cpu.thread_handle);
        fault_ordinal=0;fault_return=0;fault_stack=static_cast<sig_atomic_t>(cpu.esp);
        active_=this; entry_=entry;
        int result=0;
        try { result=entry(&cpu,mem_.Base(),address,Dispatch); }
        catch(const GuestThreadExit& e) { cpu.eax=e.code; }
        catch(...) {
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
            {std::lock_guard<std::mutex> guard(lock_);ReleaseD3dOwner(cpu.thread_handle);}
#endif
            Stop(); throw;
        }
#ifdef HULK_ENABLE_GLES_DEVICE_PROBE
        {std::lock_guard<std::mutex> guard(lock_);ReleaseD3dOwner(cpu.thread_handle);}
#endif
        if(result) Stop(); else Join();
        return result?result:worker_failure_;
    }
};
}
