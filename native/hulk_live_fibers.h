#pragma once
#include "hulk_xdk_fiber_hook.h"
#include "xbe_graphics_boundary.h"
#include <cstdio>
#include <limits>

namespace hulkpocket {
// Own the real guest thread's native C++ continuations. The root is
// registered before executing original code, then renamed to the XDK's
// TLS record on the first verified SwitchToFiber call.
class HulkLiveFibers {
    static constexpr uint32_t pending_root=0xffffffffu;
    static constexpr size_t stack_bytes=16u*1024*1024;
    X86Context& ctx_;
    uint8_t* base_;
    XbeGuestFiberBootstrap::GuestCall call_;
    HulkXdkFiberLayout layout_;
    XboxFiberScheduler scheduler_;
    XbeGuestFiberBootstrap bootstrap_;
    uint32_t root_=pending_root,entry_=0;
    unsigned switches_=0,created_=0;
    inline static thread_local HulkLiveFibers* active_=nullptr;
    static uint32_t Add(uint32_t addr,uint32_t offset){
        if(!addr||uint64_t(addr)+offset>UINT32_MAX)throw std::out_of_range("Invalid fiber address arithmetic");
        return addr+offset;
    }
    uint32_t Read(uint32_t addr){
        if(!graphics_callback)throw std::runtime_error("Fiber requires checked guest memory");
        auto probe=ctx_;probe.ecx=addr;
        graphics_callback(probe,base_,kFiberRead32);
        return probe.eax;
    }
    void Write(uint32_t addr,uint32_t value){
        if(!graphics_callback)throw std::runtime_error("Fiber requires checked guest memory");
        auto probe=ctx_;probe.ecx=addr;probe.eax=value;
        graphics_callback(probe,base_,kFiberWrite32);
    }
    static void Root(void* raw){
        auto& self=*static_cast<HulkLiveFibers*>(raw);
        self.call_(self.ctx_,self.base_,self.entry_);
    }
    static HulkLiveFibers& Require(X86Context& ctx,uint8_t* base){
        if(!active_||&active_->ctx_!=&ctx||active_->base_!=base)
            throw std::runtime_error("Fiber switch outside owning guest continuation");
        return *active_;
    }
    void Switch(){
        const auto next=Read(Add(ctx_.esp,4));
        const auto stack=Read(Add(ctx_.fs_base,4));
        const auto index=Read(layout_.fiber_tls_index_va);
        if(index>(UINT32_MAX-stack)/4)throw std::out_of_range("Invalid fiber TLS index");
        const auto tls=Read(Add(stack,index*4));
        const auto current=Read(Add(tls,0x10));
        if(!next||!current||next==pending_root||current==pending_root||ctx_.esp<20)
            throw std::runtime_error("Invalid XDK fiber record");
        // Preflight every guest address before modifying the shared CPU
        // state. A malformed frame must stop, never commit extra pages.
        Read(Add(current,0xc));
        for(unsigned off=4;off<=20;off+=4)Read(ctx_.esp-off);
        Read(ctx_.esp);
        Read(ctx_.fs_base);
        if(next!=current){
            const auto saved=Read(Add(next,0xc));
            for(unsigned off=0;off<=24;off+=4)Read(Add(saved,off));
        }
        if(root_==pending_root){
            scheduler_.AdoptCurrentId(current);
            root_=current;
            std::fprintf(stderr,"HULK FIBER ROOT guest=%08X thread=%08X\n",root_,ctx_.thread_handle);
        }
        if(current!=scheduler_.CurrentId())throw std::logic_error("Guest/native fiber ownership mismatch");
        if(!scheduler_.Exists(next)){
            const auto saved=Read(Add(next,0xc));
            const auto initial=Read(Add(saved,20));
            if(initial!=layout_.bootstrap_va)
                throw std::runtime_error("Unknown Xbox fiber continuation");
            if(created_>=64)throw std::runtime_error("Diagnostic native fiber budget exhausted");
            bootstrap_.Register(next,initial,stack_bytes);
            ++created_;
            std::fprintf(stderr,"HULK FIBER CREATE guest=%08X bootstrap=%08X\n",next,initial);
        }
        if(switches_<32)
            std::fprintf(stderr,"HULK FIBER SWITCH current=%08X next=%08X esp=%08X\n",current,next,ctx_.esp);
        ++switches_;
        HulkXdkSwitchToFiber(scheduler_,ctx_,layout_,
            [this](uint32_t a){return Read(a);},
            [this](uint32_t a,uint32_t v){Write(a,v);},&bootstrap_);
    }
public:
    HulkLiveFibers(X86Context& ctx,uint8_t* base,XbeGuestFiberBootstrap::GuestCall call,HulkXdkFiberLayout layout)
        :ctx_(ctx),base_(base),call_(call),layout_(layout),
         bootstrap_(scheduler_,ctx,base,call,true){}
    static bool Active(){return active_!=nullptr;}
    void Run(uint32_t entry){
        if(active_||!entry)throw std::logic_error("Invalid nested fiber dispatcher");
        struct Reset { ~Reset(){HulkLiveFibers::active_=nullptr;} } reset;
        active_=this;entry_=entry;
        scheduler_.Create(pending_root,&Root,this,stack_bytes);
        scheduler_.Run(pending_root);
        if(!scheduler_.Finished(root_))throw std::runtime_error("Guest fiber dispatcher stopped without root return");
    }
    static void SwitchGuest(X86Context& ctx,uint8_t* base){Require(ctx,base).Switch();}
    static void DeleteGuest(X86Context& ctx,uint8_t* base){
        auto& self=Require(ctx,base);
        const auto guest=self.Read(Add(ctx.esp,4));
        if(!guest||guest==self.root_||guest==self.scheduler_.CurrentId())
            throw std::runtime_error("Invalid active/root Xbox fiber deletion");
        if(self.scheduler_.Exists(guest)){
            self.bootstrap_.Delete(guest);
            std::fprintf(stderr,"HULK FIBER DELETE guest=%08X\n",guest);
        }
        // The strong hook invokes the original guest deletion after this.
    }
};
}
