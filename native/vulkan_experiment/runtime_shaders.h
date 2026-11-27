#pragma once
#include "shader_compiler.h"
#include "gpu/shaders.h"
#include "gpu/vertex.h"
#include "android/vertex_hlsl.h"
#include <map>
#include <thread>
namespace hulkpocket::vkexp {
// Single renderer thread owns glslang process lifetime and exact-source
// cache. Failed compilation never inserts a fallback shader.
class RuntimeShaders {
    std::thread::id owner_=std::this_thread::get_id();
    std::map<std::pair<int,std::string>,std::vector<uint32_t>> cache_;
    size_t compiled_=0,hits_=0;
    std::vector<uint32_t> Get(EShLanguage stage,std::string source){
        if(owner_!=std::this_thread::get_id())throw std::logic_error("Shader compiler owner mismatch");
        if(source.empty()||source.size()>1024*1024)throw std::invalid_argument("Invalid generated shader source");
        auto key=std::make_pair(int(stage),std::move(source));
        auto it=cache_.find(key);
        if(it!=cache_.end()){++hits_;return it->second;}
        auto code=Compile(key.second,stage);
        if(code.size()>262144)throw std::runtime_error("Generated shader exceeds size bound");
        if(cache_.size()>=128)cache_.erase(cache_.begin());
        ++compiled_;
        return cache_.emplace(std::move(key),std::move(code)).first->second;
    }
public:
    RuntimeShaders(){if(!glslang::InitializeProcess())throw std::runtime_error("glslang initialization failed");}
    ~RuntimeShaders(){glslang::FinalizeProcess();}
    RuntimeShaders(const RuntimeShaders&)=delete;
    RuntimeShaders& operator=(const RuntimeShaders&)=delete;
    size_t Compiled()const{return compiled_;}
    size_t Hits()const{return hits_;}
    std::vector<uint32_t> Vertex(const gpu::State& s,const gpu::Assembled& a){
        if(!(a.regs&1))throw std::invalid_argument("Missing position register");
        if((s.vs_handle&1)&&!s.vs)throw std::invalid_argument("Missing vertex declaration");
        if(s.vs&&!s.vs->program.empty()){
            const auto& prog=s.vs->program;
            if(s.vs->program_type!=0x2078||prog.size()%4||prog.size()>136*4||!(prog.back()&1)||a.pretransformed)
                throw std::invalid_argument("Unsupported Xbox vertex program");
            for(size_t n=0;n<prog.size();n+=4){
                auto mac=(prog[n+1]>>21)&15, out=prog[n+3];
                if(mac>13||(n+4<prog.size()&&(out&1))||((out&0xf000)&&!(out&0x800)))
                    throw std::invalid_argument("Unsupported Xbox vertex opcode or constant write");
            }
            return Get(EShLangVertex,gpu::VulkanVertexHlsl(gpu::VertexProgramHlsl(prog,a.regs,a.decode),true,false,false));
        }
        return Get(EShLangVertex,gpu::VulkanVertexHlsl(
            gpu::FixedVertexHlsl(a.regs,a.pretransformed,a.decode,gpu::MakeFixedKey(s)),false,false,false));
    }
    std::vector<uint32_t> Pixel(const gpu::State& s,const gpu::TexInfo (&tex)[4]){
        for(unsigned i=0;i<4;++i)
            if(bool(s.textures[i])!=bool(tex[i].kind)||(tex[i].kind!=gpu::kTexNone&&tex[i].kind!=gpu::kTex2D&&tex[i].kind!=gpu::kTexCube)||tex[i].shadow||tex[i].flags)
                throw std::invalid_argument("Unsupported runtime texture binding");
        auto key=gpu::MakePixelKey(s,tex);
        if(s.ps){
            if((key.count&255)>8)throw std::invalid_argument("Invalid Xbox combiner count");
            // Only modes with verified 2D shader translation. Dot,
            // cube and depth sampling still require separate integration.
            for(unsigned i=0;i<4;++i){
                auto raw=(s.rs[gpu::kRsPsTextureModes]>>(i*5))&31;
                bool dependent=raw==6||raw==7||raw==0xf||raw==0x10;
                if(raw!=0&&raw!=1&&raw!=3&&raw!=4&&raw!=5&&!dependent)
                    throw std::invalid_argument("Unsupported Vulkan programmable texture mode");
                if((raw==1||dependent)&&tex[i].kind!=gpu::kTex2D)
                    throw std::invalid_argument("Missing programmable 2D texture");
                if(raw==3&&tex[i].kind!=gpu::kTexCube)
                    throw std::invalid_argument("Missing programmable cube texture");
                if(dependent){
                    if(!i)throw std::invalid_argument("Dependent texture cannot be first");
                    unsigned source=i==1?0:(s.ps->def[56]>>(i==2?16:20))&15;
                    if(source>=i)throw std::invalid_argument("Invalid dependent texture source");
                    auto prior=(s.rs[gpu::kRsPsTextureModes]>>(5*source))&31;
                    if(prior!=1&&prior!=3&&prior!=4&&prior!=6&&prior!=7&&prior!=0xf&&prior!=0x10)
                        throw std::invalid_argument("Dependent texture source has no color");
                }
            }
        }
        return Get(EShLangFragment,gpu::PixelHlsl(key));
    }
};
}
