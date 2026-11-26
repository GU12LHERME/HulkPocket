#include "shader_compiler.h"
#include "gpu/shaders.h"
#include "gpu/vertex.h"
#include "android/vertex_hlsl.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
using namespace gpu;
using hulkpocket::vkexp::Compile;
static void Save(const std::filesystem::path& root,const char* name,const std::vector<uint32_t>& spv){
    std::ofstream f(root/name,std::ios::binary);
    f.write(reinterpret_cast<const char*>(spv.data()),std::streamsize(spv.size()*4));
    if(!f)throw std::runtime_error("SPIR-V fixture write failed");
}
static std::set<uint32_t> Locations(const std::vector<uint32_t>& spv){
    // Collect Location decorations for vertex input variables only.
    std::set<uint32_t> inputs,locations;
    for(size_t i=5;i<spv.size();){
        auto n=spv[i]>>16,op=spv[i]&65535;
        if(!n||i+n>spv.size())throw std::runtime_error("Malformed SPIR-V instruction");
        if(op==59&&n>=4&&spv[i+3]==1)inputs.insert(spv[i+2]);
        i+=n;
    }
    for(size_t i=5;i<spv.size();i+=spv[i]>>16)
        if((spv[i]&65535)==71&&(spv[i]>>16)==4&&spv[i+2]==30&&inputs.count(spv[i+1]))
            locations.insert(spv[i+3]);
    return locations;
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    if(!glslang::InitializeProcess())return 3;
    try{
        State s{};
        s.rs[kRsLighting]=s.rs[kRsNormalizeNormals]=s.rs[kRsSpecularEnable]=1;
        s.light_enable=7;s.lights[0][0]=3;s.lights[1][0]=1;s.lights[2][0]=2;
        auto fixed=MakeFixedKey(s);
        auto lit=FixedVertexHlsl(0x205,false,uint64_t(kDecodePacked)<<8,fixed);
        lit=VulkanVertexHlsl(std::move(lit),false,false,false);
        auto a=Compile(lit,EShLangVertex);
        assert(Locations(a).contains(0)&&Locations(a).contains(2)&&Locations(a).contains(9));
        Save(root,"packed-lit.vert.spv",a);
        s.light_enable=1;
        auto directional=VulkanVertexHlsl(FixedVertexHlsl(0x205,false,uint64_t(kDecodePacked)<<8,MakeFixedKey(s)),false,false,false);
        Save(root,"packed-directional.vert.spv",Compile(directional,EShLangVertex));
        auto unlit=Compile(VulkanVertexHlsl(FixedVertexHlsl(0x209,false),false,false,false),EShLangVertex);
        assert(Locations(unlit).contains(0)&&Locations(unlit).contains(3)&&Locations(unlit).contains(9));
        Save(root,"diffuse-uv.vert.spv",unlit);
        Save(root,"screen.vert.spv",Compile(VulkanVertexHlsl(FixedVertexHlsl(0x209,true),false,false,false),EShLangVertex));
        TexInfo tex[4]{};State p{};
        p.tss[0][kTssColorOp]=p.tss[0][kTssAlphaOp]=1;
        Save(root,"untextured.frag.spv",Compile(PixelHlsl(MakePixelKey(p,tex)),EShLangFragment));
        tex[0].kind=kTex2D;p.textures[0]=1;p.vs_handle=0x142;
        p.tss[0][kTssColorOp]=p.tss[0][kTssAlphaOp]=4;
        p.tss[0][kTssColorArg1]=p.tss[0][kTssAlphaArg1]=0;
        p.tss[0][kTssColorArg2]=p.tss[0][kTssAlphaArg2]=2;
        p.tss[0][kTssResultArg]=1;p.simple[kMAlphaTestEnable>>2]=1;
        Save(root,"modulate.frag.spv",Compile(PixelHlsl(MakePixelKey(p,tex)),EShLangFragment));
        // Synthetic two-stage combiner, not evidence of a captured Hulk draw.
        tex[1].kind=kTex2D;p.textures[1]=2;p.vs_handle=0x242;
        p.tss[1]=p.tss[0];p.tss[1][kTssColorArg1]=1;p.tss[1][kTssTexCoordIndex]=1;
        Save(root,"two-stage.frag.spv",Compile(PixelHlsl(MakePixelKey(p,tex)),EShLangFragment));
        // Unknown program bodies are not synthesized or declared supported.
        std::puts("PASS Hulk fixed-function shaders compiled to SPIR-V; guest attribute locations verified");
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());glslang::FinalizeProcess();return 1;}
    glslang::FinalizeProcess();
}
