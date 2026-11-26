#pragma once
#include "gpu/backend.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <array>
#include <thread>

namespace hulkpocket {
// First real renderer slice: a single offscreen color/depth/stencil target,
// fixed-function diffuse 3D or pretransformed vertices with optional texture. Not a general Xbox renderer.
// All operations are synchronous on the EGL owner thread.
class XbeGlesBackend final:public gpu::Backend {
    EGLDisplay display_=EGL_NO_DISPLAY;
    EGLContext context_=EGL_NO_CONTEXT;
    EGLSurface surface_=EGL_NO_SURFACE;
    GLuint program_=0,vao_=0,vbo_=0,texture_=0;
    GLint extent_=-1, textured_=-1, sampler_=-1, color_op_=-1, alpha_op_=-1, transformed_=-1, world_=-1, view_=-1, projection_=-1;
    uint32_t width_=0,height_=0,target_=0, depth_target_=0;
    bool depth_cleared_=false;
    uint32_t fence_=0;
    std::thread::id owner_{};
    void Check() const;
    void Target(const gpu::Surface&);
    void DepthTarget(const gpu::Surface&);
    void RasterState(const gpu::State&);
    void UploadTexture(const gpu::State&);
    void Destroy() noexcept;
public:
    XbeGlesBackend()=default;
    ~XbeGlesBackend() override;
    XbeGlesBackend(const XbeGlesBackend&)=delete;
    XbeGlesBackend& operator=(const XbeGlesBackend&)=delete;
    bool Init(uint32_t,uint32_t) override;
    void Clear(const gpu::State&,uint32_t,const uint8_t*,uint32_t,uint32_t,float,uint32_t) override;
    void Draw(const gpu::State&,uint32_t,uint32_t,uint32_t,const uint8_t*) override;
    void Present(const gpu::Surface&) override;
    void Copy(const gpu::Surface&,const gpu::Surface&,uint32_t,uint32_t,uint32_t,float,float) override;
    void Sync(uint32_t,uint32_t) override;
    void SyncLate(uint32_t) override;
    void Flush() override;
    void SetGamma(const uint8_t*) override;
    void ClearReport() override;
    void GetReport(uint32_t) override;
    uint32_t Fence() override;
    bool FencePending(uint32_t) override;
    void WaitFence(uint32_t) override;
    bool Busy(uint32_t) override;
    void WaitResource(uint32_t) override;
    void WaitGpuFrame(uint64_t) override;
    std::array<uint8_t,4> ReadPixel(uint32_t x,uint32_t y);
};
}
