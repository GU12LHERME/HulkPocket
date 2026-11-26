#include "xbe_gles_backend.h"
#include "gpu/texture.h"
#include "gpu/guest_memory.h"
#include "xbe_bc_decode.h"
#include "gpu/vertex.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace hulkpocket {
namespace {
[[noreturn]] void Unsupported(const char* what){throw std::logic_error(what);}
void GlCheck(const char* where){
    auto e=glGetError();
    if(e!=GL_NO_ERROR)throw std::runtime_error(std::string(where)+" GL error "+std::to_string(e));
}
GLuint Shader(GLenum type,const char* source){
    auto s=glCreateShader(type);
    if(!s)throw std::runtime_error("glCreateShader failed");
    glShaderSource(s,1,&source,nullptr);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);glDeleteShader(s);throw std::runtime_error(log);}
    return s;
}
constexpr const char* vs=R"(#version 300 es
layout(location=0) in vec4 a_position;
layout(location=1) in vec4 a_color;
layout(location=2) in vec2 a_uv;
uniform vec2 u_extent;
out vec4 v_color;
out vec2 v_uv;
void main(){
    float w=1.0/a_position.w;
    vec2 ndc=vec2(2.0*a_position.x/u_extent.x-1.0,1.0-2.0*a_position.y/u_extent.y);
    gl_Position=vec4(ndc*w,(2.0*a_position.z-1.0)*w,w);
    v_color=a_color;v_uv=a_uv;
})";
constexpr const char* fs=R"(#version 300 es
precision mediump float;
in vec4 v_color;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform int u_textured;
uniform int u_color_op;
uniform int u_alpha_op;
out vec4 color;
void main(){
    color=v_color;
    if(u_textured!=0){
        vec4 t=texture(u_texture,v_uv);
        color.rgb=u_color_op==2?t.rgb:t.rgb*v_color.rgb;
        color.a=u_alpha_op==2?t.a:u_alpha_op==4?t.a*v_color.a:v_color.a;
    }
})";
struct Vertex {float p[4];float c[4];float uv[2];};
}
XbeGlesBackend::~XbeGlesBackend(){Destroy();}
void XbeGlesBackend::Destroy() noexcept {
    if(display_==EGL_NO_DISPLAY)return;
    if(owner_!=std::this_thread::get_id())return;
    if(context_!=EGL_NO_CONTEXT && eglGetCurrentContext()==context_){
        if(texture_)glDeleteTextures(1,&texture_);
        if(vbo_)glDeleteBuffers(1,&vbo_);
        if(vao_)glDeleteVertexArrays(1,&vao_);
        if(program_)glDeleteProgram(program_);
        eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    }
    if(context_!=EGL_NO_CONTEXT)eglDestroyContext(display_,context_);
    if(surface_!=EGL_NO_SURFACE)eglDestroySurface(display_,surface_);
    eglTerminate(display_);
    display_=EGL_NO_DISPLAY;context_=EGL_NO_CONTEXT;surface_=EGL_NO_SURFACE;
    program_=vao_=vbo_=texture_=0;width_=height_=target_=fence_=0;
}
void XbeGlesBackend::Check() const {
    if(context_==EGL_NO_CONTEXT || owner_!=std::this_thread::get_id() ||
       eglGetCurrentContext()!=context_)throw std::logic_error("GLES backend requires its current owner context");
}
bool XbeGlesBackend::Init(uint32_t w,uint32_t h){
    if(display_!=EGL_NO_DISPLAY||!w||!h||w>4096||h>4096)return false;
    owner_=std::this_thread::get_id();
    try{
        display_=eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if(display_==EGL_NO_DISPLAY||!eglInitialize(display_,nullptr,nullptr))throw std::runtime_error("EGL init failed");
        if(!eglBindAPI(EGL_OPENGL_ES_API))throw std::runtime_error("EGL ES bind failed");
        const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_STENCIL_SIZE,8,EGL_NONE};
        EGLConfig cfg{};EGLint n=0;
        if(!eglChooseConfig(display_,attrs,&cfg,1,&n)||!n)throw std::runtime_error("No EGL ES3 pbuffer configuration");
        const EGLint pbuf[]={EGL_WIDTH,EGLint(w),EGL_HEIGHT,EGLint(h),EGL_NONE};
        surface_=eglCreatePbufferSurface(display_,cfg,pbuf);
        const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
        context_=eglCreateContext(display_,cfg,EGL_NO_CONTEXT,ctx);
        if(surface_==EGL_NO_SURFACE||context_==EGL_NO_CONTEXT||
           !eglMakeCurrent(display_,surface_,surface_,context_))throw std::runtime_error("EGL context failed");
        GLuint v=Shader(GL_VERTEX_SHADER,vs),f=0;
        try{f=Shader(GL_FRAGMENT_SHADER,fs);}catch(...){glDeleteShader(v);throw;}
        program_=glCreateProgram();glAttachShader(program_,v);glAttachShader(program_,f);
        glLinkProgram(program_);glDeleteShader(v);glDeleteShader(f);
        GLint ok=0;glGetProgramiv(program_,GL_LINK_STATUS,&ok);
        if(!ok)throw std::runtime_error("GLES program link failed");
        extent_=glGetUniformLocation(program_,"u_extent");
        textured_=glGetUniformLocation(program_,"u_textured");
        sampler_=glGetUniformLocation(program_,"u_texture");
        color_op_=glGetUniformLocation(program_,"u_color_op");
        alpha_op_=glGetUniformLocation(program_,"u_alpha_op");
        if(extent_<0||textured_<0||sampler_<0||color_op_<0||alpha_op_<0)throw std::runtime_error("GLES uniform missing");
        glGenTextures(1,&texture_);
        glGenVertexArrays(1,&vao_);glGenBuffers(1,&vbo_);
        glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
        glEnableVertexAttribArray(1);glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(sizeof(float)*4));
        glEnableVertexAttribArray(2);glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(sizeof(float)*8));
        glViewport(0,0,w,h);glDisable(GL_DITHER);
        GlCheck("GLES init");width_=w;height_=h;
        std::fprintf(stderr,"GLES renderer: %s\n",reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        return true;
    }catch(const std::exception& e){std::fprintf(stderr,"GLES init: %s (EGL %x)\n",e.what(),eglGetError());Destroy();return false;}
}
void XbeGlesBackend::Target(const gpu::Surface& s){
    if(!s.header)Unsupported("GLES color target required");
    gpu::TextureDesc d{};
    if(!gpu::DescribeSurface(s,d)||d.width!=width_||d.height!=height_||d.format!=0x12||!d.linear)
        Unsupported("GLES diagnostic requires one matching linear A8R8G8B8 target");
    if(target_&&target_!=s.header)Unsupported("GLES multiple targets not implemented");
    target_=s.header;
}
void XbeGlesBackend::Clear(const gpu::State& s,uint32_t count,const uint8_t* rects,uint32_t flags,uint32_t color,float z,uint32_t stencil){
    Check();Target(s.rt);
    if(count||rects)Unsupported("GLES rectangle clear not implemented");
    if(flags&~(gpu::kClearTarget|gpu::kClearZ|gpu::kClearStencil))Unsupported("Unsupported clear flags");
    if(flags&(gpu::kClearZ|gpu::kClearStencil))Unsupported("Guest depth surface not implemented");
    if(!std::isfinite(z))throw std::invalid_argument("Nonfinite clear depth");
    glDisable(GL_SCISSOR_TEST);
    // NV2A D3DCLEAR_TARGET comprises R/G/B/A channel bits 4..7.
    glColorMask((flags&0x10)!=0,(flags&0x20)!=0,(flags&0x40)!=0,(flags&0x80)!=0);
    glClearColor(float((color>>16)&255)/255,float((color>>8)&255)/255,float(color&255)/255,float(color>>24)/255);
    glClearDepthf(z);glClearStencil(GLint(stencil));
    if(flags&gpu::kClearTarget)glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);GlCheck("GLES clear");
}
void XbeGlesBackend::UploadTexture(const gpu::State& s){
    gpu::TextureDesc d{};
    if(!gpu::Describe(s.textures[0],d)||d.depth!=1||d.cube)
        Unsupported("Only Xbox 2D textures are supported");
    auto f=gpu::Format(d.format);
    if(f.host!=gpu::Host::BGRA8&&!f.compressed)Unsupported("Unsupported GLES texture format");
    if(d.format==0x0b)Unsupported("Palette resource mapping pending");
    gpu::TextureDesc rt{};
    if(!gpu::Describe(s.rt.header,rt))Unsupported("Missing render target");
    // Guest render target readback is not implemented, so reject aliases.
    std::vector<uint8_t> decoded;uint32_t pitch=0;
    gpu::DecodeGuestLevel(d,0,0,0,decoded,pitch);
    uint64_t a=d.data,b=rt.data;
    if(a<b+gpu::TotalSize(rt)&&b<a+gpu::TotalSize(d))
        Unsupported("Render-to-texture synchronization pending");
    if(f.compressed)decoded=DecodeXboxBc(d.width,d.height,f.host,decoded);
    else for(size_t i=0;i<decoded.size();i+=4)std::swap(decoded[i],decoded[i+2]);
    if(decoded.size()!=uint64_t(d.width)*d.height*4)throw std::runtime_error("Invalid RGBA texture size");
    const auto& st=s.tss[0];
    auto address=[](uint32_t v)->GLint{
        switch(v){case 1:return GL_REPEAT;case 2:return GL_MIRRORED_REPEAT;case 3:return GL_CLAMP_TO_EDGE;
        default:Unsupported("Unsupported texture address mode");}
    };
    auto filter=[](uint32_t v)->GLint{
        switch(v){case 1:return GL_NEAREST;case 2:return GL_LINEAR;default:Unsupported("Unsupported texture filter");}
    };
    auto u=address(st[gpu::kTssAddressU]),v=address(st[gpu::kTssAddressV]);
    auto min=filter(st[gpu::kTssMinFilter]),mag=filter(st[gpu::kTssMagFilter]);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture_);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,u);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,v);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,min);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,mag);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    // Re-upload for correctness until lock/dirty tracking is linked to actual guest resource calls.
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,d.width,d.height,0,GL_RGBA,GL_UNSIGNED_BYTE,decoded.data());
    GlCheck("GLES texture upload");
}
void XbeGlesBackend::Draw(const gpu::State& s,uint32_t prim,uint32_t start,uint32_t count,const uint8_t* indices){
    Check();Target(s.rt);
    const bool textured=s.textures[0]!=0;
    if(s.vs_handle!=(textured?0x144u:0x44u)||s.vs||s.ps||s.scissor_count||s.zs.header||
       s.viewport.x||s.viewport.y||s.viewport.w!=width_||s.viewport.h!=height_||
       s.viewport.min_z!=0||s.viewport.max_z!=1||s.ss_offset_x!=0||s.ss_offset_y!=0||
       std::any_of(s.textures.begin()+1,s.textures.end(),[](auto v){return v!=0;})||
       std::any_of(s.rs.begin(),s.rs.end(),[](auto v){return v!=0;})||
       std::any_of(s.simple.begin(),s.simple.end(),[](auto v){return v!=0;})||
       std::any_of(s.tss.begin()+1,s.tss.end(),[](const auto& stage){return std::any_of(stage.begin(),stage.end(),[](auto v){return v!=0;});}))
        Unsupported("GLES diagnostic requires XYZRHW diffuse and a single optional texture");
    const auto& st=s.tss[0];
    for(unsigned i=0;i<st.size();++i){
        if(!st[i])continue;
        switch(i){
            case gpu::kTssAddressU:case gpu::kTssAddressV:case gpu::kTssMinFilter:case gpu::kTssMagFilter:
            case gpu::kTssColorOp:case gpu::kTssColorArg1:case gpu::kTssColorArg2:
            case gpu::kTssAlphaOp:case gpu::kTssAlphaArg1:case gpu::kTssAlphaArg2:break;
            default:Unsupported("Unsupported stage-0 texture state");
        }
    }
    auto op=st[gpu::kTssColorOp],alpha=st[gpu::kTssAlphaOp];
    if(textured){
        if((op!=2&&op!=4)||st[gpu::kTssColorArg1]!=2||
           (op==4&&st[gpu::kTssColorArg2]!=0)||
           (alpha!=2&&alpha!=3&&alpha!=4)||st[gpu::kTssAlphaArg1]!=2||
           ((alpha==3||alpha==4)&&st[gpu::kTssAlphaArg2]!=0))
            Unsupported("Unsupported fixed-function texture combiner");
    } else if(op>1||alpha>1)Unsupported("Active texture combiner without a texture");
    if(!count)return;
    if(prim!=5)Unsupported("GLES diagnostic only accepts triangle lists");
    gpu::Assembled a;
    if(!gpu::Assemble(s,prim,start,count,indices,a))throw std::runtime_error("Vertex assembly failed");
    if(a.regs!=(textured?0x209u:9u)||!a.pretransformed||a.elements[0].format!=gpu::kVertexR32G32B32A32_FLOAT||
       a.elements[3].format!=gpu::kVertexB8G8R8A8_UNORM||
       (textured&&a.elements[9].format!=gpu::kVertexR32G32_FLOAT))Unsupported("Unsupported GLES vertex layout");
    uint32_t n=a.index_count?a.index_count:a.draw_count;
    if(n%3||n>262144)throw std::invalid_argument("Invalid triangle count");
    std::vector<Vertex> vertices(n);
    for(uint32_t i=0;i<n;++i){
        int64_t id=i;
        if(a.index_count){
            if(a.index16){uint16_t v;std::memcpy(&v,static_cast<const uint8_t*>(a.index_src)+i*2,2);id=int64_t(v)+a.base_vertex;}
            else{uint32_t v;std::memcpy(&v,static_cast<const uint8_t*>(a.index_src)+i*4,4);id=int64_t(v)+a.base_vertex;}
        }
        if(id<0||uint64_t(id)>=a.vertex_count)throw std::out_of_range("Invalid assembled index");
        auto read=[&](unsigned reg,void* dest,size_t bytes){
            const auto& e=a.elements[reg];if(e.buffer>=a.buffer_count)throw std::out_of_range("Invalid assembled buffer");
            const auto& b=a.buffers[e.buffer];uint64_t off=uint64_t(id)*b.stride+e.offset;
            if(off>b.bytes||bytes>b.bytes-off)throw std::out_of_range("Invalid assembled vertex");
            std::memcpy(dest,b.src+off,bytes);
        };
        auto& v=vertices[i];read(0,v.p,16);
        uint8_t bgra[4];read(3,bgra,4);
        if(textured){read(9,v.uv,8);if(!std::isfinite(v.uv[0])||!std::isfinite(v.uv[1]))throw std::invalid_argument("Invalid texture coordinates");}
        if(!std::all_of(v.p,v.p+4,[](float x){return std::isfinite(x);})||v.p[3]<=0)
            throw std::invalid_argument("Invalid XYZRHW position");
        v.c[0]=bgra[2]/255.0f;v.c[1]=bgra[1]/255.0f;v.c[2]=bgra[0]/255.0f;v.c[3]=bgra[3]/255.0f;
    }
    glViewport(0,0,width_,height_);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    if(textured)UploadTexture(s);
    glUseProgram(program_);glUniform2f(extent_,float(width_),float(height_));
    glUniform1i(textured_,textured);glUniform1i(sampler_,0);glUniform1i(color_op_,op);glUniform1i(alpha_op_,alpha);
    glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(vertices.size()*sizeof(Vertex)),vertices.data(),GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES,0,GLsizei(n));GlCheck("GLES draw");
}
void XbeGlesBackend::Present(const gpu::Surface& s){Check();Target(s);glFinish();GlCheck("GLES present");if(!eglSwapBuffers(display_,surface_))throw std::runtime_error("EGL present failed");}
void XbeGlesBackend::Flush(){Check();glFinish();GlCheck("GLES finish");}
std::array<uint8_t,4> XbeGlesBackend::ReadPixel(uint32_t x,uint32_t y){
    Check();if(x>=width_||y>=height_)throw std::out_of_range("Pixel outside framebuffer");
    std::array<uint8_t,4> pixel{};glReadPixels(x,height_-1-y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());GlCheck("GLES readback");return pixel;
}
void XbeGlesBackend::Copy(const gpu::Surface&,const gpu::Surface&,uint32_t,uint32_t,uint32_t,float,float){Unsupported("GLES surface copies pending");}
void XbeGlesBackend::Sync(uint32_t,uint32_t){Unsupported("GLES guest readback pending");}
void XbeGlesBackend::SyncLate(uint32_t){Unsupported("GLES asynchronous readback pending");}
void XbeGlesBackend::SetGamma(const uint8_t*){Unsupported("GLES gamma pending");}
void XbeGlesBackend::ClearReport(){Unsupported("GLES visibility queries pending");}
void XbeGlesBackend::GetReport(uint32_t){Unsupported("GLES visibility queries pending");}
uint32_t XbeGlesBackend::Fence(){Flush();return ++fence_;}
bool XbeGlesBackend::FencePending(uint32_t f){Check();if(!f||f>fence_)throw std::out_of_range("Unknown GLES fence");return false;}
void XbeGlesBackend::WaitFence(uint32_t f){(void)FencePending(f);}
bool XbeGlesBackend::Busy(uint32_t){Unsupported("GLES resource busy tracking pending");}
void XbeGlesBackend::WaitResource(uint32_t){Unsupported("GLES resource synchronization pending");}
void XbeGlesBackend::WaitGpuFrame(uint64_t){Unsupported("GLES frame scheduling pending");}
}
