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
        if(s.vs&&!s.vs->program.empty())
            throw std::invalid_argument("Programmable vertex constants not integrated");
        return Get(EShLangVertex,gpu::VulkanVertexHlsl(
            gpu::FixedVertexHlsl(a.regs,a.pretransformed,a.decode,gpu::MakeFixedKey(s)),false,false,false));
    }
    std::vector<uint32_t> Pixel(const gpu::State& s,const gpu::TexInfo (&tex)[4]){
        if(s.ps)throw std::invalid_argument("Programmable pixel constants not integrated");
        for(unsigned i=0;i<4;++i)
            if(bool(s.textures[i])!=bool(tex[i].kind)||tex[i].kind>gpu::kTex2D||tex[i].shadow||tex[i].flags)
                throw std::invalid_argument("Unsupported runtime texture binding");
        return Get(EShLangFragment,gpu::PixelHlsl(gpu::MakePixelKey(s,tex)));
    }
};
}
