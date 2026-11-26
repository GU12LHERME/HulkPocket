#pragma once
#include "xbe_kernel_pool_calls.h"
namespace hulkpocket {
class XbeKernelSession {
    XboxGuestAddressSpace& mem_;
    XbeKernelPool pool_;
    inline static thread_local XbeKernelSession* active_=nullptr;
public:
    XbeKernelSession(XboxGuestAddressSpace& mem,uint32_t base,uint32_t size)
        :mem_(mem),pool_(mem,base,size) {}
    XbeKernelSession(const XbeKernelSession&)=delete;
    XbeKernelSession& operator=(const XbeKernelSession&)=delete;
    using Callback=void(*)(X86Context&,uint8_t*,uint32_t);
    using Entry=int(*)(X86Context*,uint8_t*,uint32_t,Callback);
    static void Dispatch(X86Context& cpu,uint8_t* base,uint32_t ordinal) {
        if(!active_ || active_->mem_.Base()!=base)
            throw std::runtime_error("Invalid kernel session");
        if(!DispatchPoolCall(ordinal,cpu,active_->mem_,active_->pool_))
            throw std::runtime_error("Unsupported kernel ordinal "+std::to_string(ordinal));
    }
    int Run(Entry entry,X86Context& cpu,uint32_t address) {
        if(!entry || active_) throw std::runtime_error("Invalid nested kernel session");
        struct Reset { ~Reset(){active_=nullptr;} } reset;
        active_=this;
        return entry(&cpu,mem_.Base(),address,Dispatch);
    }
};
}
