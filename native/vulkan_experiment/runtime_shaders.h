#pragma once
#include "shader_compiler.h"
#include "gpu/shaders.h"
#include "gpu/vertex.h"
#include "android/vertex_hlsl.h"
#include "fixed_two_sided.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <thread>
#include <type_traits>
namespace hulkpocket::vkexp {
// Compiled SPIR-V plus a run-unique id. Pipelines key on ids instead of
// the full SPIR-V words; an evicted and recompiled shader gets a new id.
struct RuntimeShader {
    std::shared_ptr<const std::vector<uint32_t>> code;
    uint64_t id=0;
};
// Single renderer thread owns glslang process lifetime. The cache is keyed by
// the exact inputs the shared generators are pure functions of (fixed/pixel
// keys without padding, program words, register masks), so a hit skips both
// HLSL generation and compilation. Failed compilation never inserts a
// fallback shader.
class RuntimeShaders {
    std::thread::id owner_=std::this_thread::get_id();
    // A glslang compile costs milliseconds on the RP6; keep a large cache
    // with true LRU eviction.
    static constexpr size_t CacheLimit=1024;
    struct Entry {RuntimeShader shader;uint64_t used=0;};
    std::map<std::string,Entry> cache_;
    size_t compiled_=0,hits_=0,evicted_=0;uint64_t tick_=0,next_id_=0;
    static_assert(std::has_unique_object_representations_v<gpu::FixedKey>,"FixedKey must have no padding to serve as a cache key");
    static_assert(std::has_unique_object_representations_v<gpu::PixelKey>,"PixelKey must have no padding to serve as a cache key");
    template<class T> static void Append(std::string& k,const T& v){k.append(reinterpret_cast<const char*>(&v),sizeof v);}
    template<class Generate>
    RuntimeShader Get(EShLanguage stage,std::string key,Generate&& generate){
        if(owner_!=std::this_thread::get_id())throw std::logic_error("Shader compiler owner mismatch");
        key.insert(key.begin(),char(stage));
        auto it=cache_.find(key);
        if(it!=cache_.end()){++hits_;it->second.used=++tick_;return it->second.shader;}
        const std::string source=generate();
        if(source.empty()||source.size()>1024*1024)throw std::invalid_argument("Invalid generated shader source");
        auto code=Compile(source,stage);
        if(code.size()>262144)throw std::runtime_error("Generated shader exceeds size bound");
        if(cache_.size()>=CacheLimit){
            cache_.erase(std::min_element(cache_.begin(),cache_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;}));
            ++evicted_;
        }
        ++compiled_;
        RuntimeShader shader{std::make_shared<const std::vector<uint32_t>>(std::move(code)),++next_id_};
        return cache_.emplace(std::move(key),Entry{shader,++tick_}).first->second.shader;
    }
public:
    RuntimeShaders(){if(!glslang::InitializeProcess())throw std::runtime_error("glslang initialization failed");}
    ~RuntimeShaders(){glslang::FinalizeProcess();}
    RuntimeShaders(const RuntimeShaders&)=delete;
    RuntimeShaders& operator=(const RuntimeShaders&)=delete;
    size_t Compiled()const{return compiled_;}
    size_t Hits()const{return hits_;}
    size_t Evicted()const{return evicted_;}
    RuntimeShader Vertex(const gpu::State& s,const gpu::Assembled& a){
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
            std::string k("P");Append(k,a.regs);Append(k,a.decode);
            k.append(reinterpret_cast<const char*>(prog.data()),prog.size()*sizeof(prog[0]));
            return Get(EShLangVertex,std::move(k),[&]{return gpu::VulkanVertexHlsl(gpu::VertexProgramHlsl(prog,a.regs,a.decode),true,false,false);});
        }
        const auto key=gpu::MakeFixedKey(s);
        const bool two_sided=hulkpocket::vkexp::TwoSidedLit(s);
        std::string k(two_sided?"T":"F");Append(k,a.regs);Append(k,a.decode);Append(k,uint8_t(a.pretransformed));Append(k,key);
        return Get(EShLangVertex,std::move(k),[&]{
            return gpu::VulkanVertexHlsl(two_sided?
                hulkpocket::vkexp::TwoSidedFixedVertexHlsl(a.regs,a.pretransformed,a.decode,key):
                gpu::FixedVertexHlsl(a.regs,a.pretransformed,a.decode,key),false,false,false);});
    }
    RuntimeShader Pixel(const gpu::State& s,const gpu::TexInfo (&tex)[4]){
        for(unsigned i=0;i<4;++i)
            if(bool(s.textures[i])!=bool(tex[i].kind)||(tex[i].kind!=gpu::kTexNone&&tex[i].kind!=gpu::kTex2D&&tex[i].kind!=gpu::kTex3D&&tex[i].kind!=gpu::kTexCube)||(tex[i].shadow&&(tex[i].shadow>2||tex[i].kind!=gpu::kTex2D))||tex[i].flags)
                throw std::invalid_argument("Unsupported runtime texture binding");
        auto key=gpu::MakePixelKey(s,tex);
        if(s.ps){
            if((key.count&255)>8)throw std::invalid_argument("Invalid Xbox combiner count");
            // Direct 2D, 3D and cube sampling, plus the checked dependent
            // modes. Dot-product and depth modes remain guarded.
            for(unsigned i=0;i<4;++i){
                auto raw=(s.rs[gpu::kRsPsTextureModes]>>(i*5))&31;
                bool dependent=raw==6||raw==7||raw==0xf||raw==0x10;
                if(raw!=0&&raw!=1&&raw!=2&&raw!=3&&raw!=4&&raw!=5&&!dependent)
                    throw std::invalid_argument("Unsupported Vulkan programmable texture mode");
                if((raw==1||dependent)&&tex[i].kind!=gpu::kTex2D)
                    throw std::invalid_argument("Missing programmable 2D texture");
                if(tex[i].shadow&&raw!=2)throw std::invalid_argument("Depth texture requires project3D mode");
                if(raw==2&&tex[i].kind!=gpu::kTex3D&&!tex[i].shadow)
                    throw std::invalid_argument("Missing programmable volume texture");
                if(raw==3&&tex[i].kind!=gpu::kTexCube)
                    throw std::invalid_argument("Missing programmable cube texture");
                if(dependent){
                    if(!i)throw std::invalid_argument("Dependent texture cannot be first");
                    unsigned source=i==1?0:(s.ps->def[56]>>(i==2?16:20))&15;
                    if(source>=i)throw std::invalid_argument("Invalid dependent texture source");
                    auto prior=(s.rs[gpu::kRsPsTextureModes]>>(5*source))&31;
                    if(prior!=1&&prior!=2&&prior!=3&&prior!=4&&prior!=6&&prior!=7&&prior!=0xf&&prior!=0x10)
                        throw std::invalid_argument("Dependent texture source has no color");
                }
            }
        }
        std::string k("X");Append(k,key);
        return Get(EShLangFragment,std::move(k),[&]{return gpu::PixelHlsl(key);});
    }
};
}
