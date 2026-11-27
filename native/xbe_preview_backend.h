#pragma once
#include "xbe_gles_backend.h"
#ifdef HULK_ENABLE_VULKAN_PREVIEW
#include "vulkan_experiment/vulkan_bridge_backend.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <memory>
namespace hulkpocket {
// Selected once per guest device on its owning thread. No mid-frame fallback.
class XbePreviewBackend final:public gpu::Backend {
    std::unique_ptr<XbeGlesBackend> gl_;
    std::unique_ptr<vkexp::VulkanBridgeBackend> vk_;
    gpu::Backend& B(){return vk_?static_cast<gpu::Backend&>(*vk_):static_cast<gpu::Backend&>(*gl_);}
public:
    XbePreviewBackend(){
        const char* v=std::getenv("HULK_RENDERER");
        if(v&&std::strcmp(v,"vulkan")==0)vk_=std::make_unique<vkexp::VulkanBridgeBackend>();
        else gl_=std::make_unique<XbeGlesBackend>();
        std::fprintf(stderr,"HULK RENDERER %s\n",vk_?"Vulkan experimental":"OpenGL ES");
    }
    bool Init(uint32_t w,uint32_t h)override{return B().Init(w,h);}
    void Clear(const gpu::State& s,uint32_t n,const uint8_t* p,uint32_t f,uint32_t c,float z,uint32_t t)override{B().Clear(s,n,p,f,c,z,t);}
    void Draw(const gpu::State& s,uint32_t p,uint32_t a,uint32_t n,const uint8_t* i)override{B().Draw(s,p,a,n,i);}
    void DrawAssembled(const gpu::State& s,uint32_t p,uint32_t a,uint32_t n,const uint8_t* i,const gpu::Assembled& v)override{
        B().DrawAssembled(s,p,a,n,i,v);
    }
    void Copy(const gpu::Surface& s,const gpu::Surface& d,uint32_t n,uint32_t r,uint32_t p,float x,float y)override{B().Copy(s,d,n,r,p,x,y);}
    void Present(const gpu::Surface& s)override{B().Present(s);}
    void Sync(uint32_t a,uint32_t n)override{B().Sync(a,n);}
    void SyncLate(uint32_t a)override{B().SyncLate(a);}
    bool DeliverLate(uint32_t a)override{return B().DeliverLate(a);}
    void RecordLate(uint32_t a)override{B().RecordLate(a);}
    void Flush()override{B().Flush();}
    void SetGamma(const uint8_t* p)override{B().SetGamma(p);}
    void ClearReport()override{B().ClearReport();}
    void GetReport(uint32_t a)override{B().GetReport(a);}
    uint32_t Fence()override{return B().Fence();}
    bool FencePending(uint32_t a)override{return B().FencePending(a);}
    void WaitFence(uint32_t a)override{B().WaitFence(a);}
    bool Busy(uint32_t a)override{return B().Busy(a);}
    void WaitResource(uint32_t a)override{B().WaitResource(a);}
    void WaitGpuFrame(uint64_t a)override{B().WaitGpuFrame(a);}
    std::array<uint8_t,4> ReadPixel(uint32_t x,uint32_t y){return vk_?vk_->ReadPixel(x,y):gl_->ReadPixel(x,y);}
    void ReadbackColor(const gpu::Surface& s){if(vk_)vk_->ReadbackColor(s);else gl_->ReadbackColor(s);}
    void BeginRotatedFrame(const gpu::Surface& s){if(vk_)vk_->BeginRotatedFrame(s);else gl_->BeginRotatedFrame(s);}
    void RestoreRotatedColor(const gpu::Surface& s){if(vk_)vk_->RestoreRotatedColor(s);else gl_->RestoreRotatedColor(s);}
};
}
#else
namespace hulkpocket {using XbePreviewBackend=XbeGlesBackend;}
#endif
