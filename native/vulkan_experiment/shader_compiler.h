#pragma once
// Adapted from SCDA Android VulkanBackend::Module and ForGlslang,
// a81808004c397cbee282031933cdda2972ffad4e (MIT, see third_party/scda-vulkan/LICENSE).
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <cstdint>
#include <utility>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>
namespace hulkpocket::vkexp {
inline std::string ForGlslang(std::string s){
    for(size_t p;(p=s.find("1.#INF"))!=std::string::npos;)
        s.replace(p,6,"asfloat(0x7F800000)");
    static const std::regex attr(R"((float4|int4|uint) v(\d+) : ATTR\d+;)");
    s=std::regex_replace(s,attr,"[[vk::location($2)]] $1 v$2 : ATTR$2;");
    // Separate stage compilation can remove unused varyings. Keep the
    // generated VS/PS interface stable regardless of dead-code elimination.
    static const std::regex color(R"((float4 [db][01] : COLOR([0-3]);))");
    s=std::regex_replace(s,color,"[[vk::location($2)]] $1");
    for(unsigned i=0;i<6;++i){
        const std::string semantic=" : TEXCOORD"+std::to_string(i)+";";
        auto at=s.find(semantic);
        if(at==std::string::npos)continue;
        auto line=s.rfind('\n',at);
        if(line==std::string::npos)throw std::runtime_error("Malformed varying declaration");
        s.insert(line+1,"    [[vk::location("+std::to_string(4+i)+")]] ");
    }
    return s;
}
// Initialize/finalize glslang once in the owning renderer, not per shader.
// Call on a serialized renderer thread. Compilation failure is not a draw.
inline std::vector<uint32_t> Compile(std::string hlsl,EShLanguage stage){
    hlsl=ForGlslang(std::move(hlsl));
    glslang::TShader sh(stage);const char* src=hlsl.c_str();sh.setStrings(&src,1);
    sh.setEntryPoint("main");sh.setSourceEntryPoint("main");
    sh.setEnvInput(glslang::EShSourceHlsl,stage,glslang::EShClientVulkan,100);
    sh.setEnvClient(glslang::EShClientVulkan,glslang::EShTargetVulkan_1_1);
    sh.setEnvTarget(glslang::EShTargetSpv,glslang::EShTargetSpv_1_3);
    sh.setHlslIoMapping(true);sh.setAutoMapLocations(true);
    sh.setShiftBinding(glslang::EResUbo,0);
    sh.setShiftBinding(glslang::EResTexture,2);
    sh.setShiftBinding(glslang::EResSampler,6);
    auto messages=EShMessages(EShMsgSpvRules|EShMsgVulkanRules|EShMsgReadHlsl|EShMsgHlslOffsets);
    glslang::TProgram prog;
    bool ok=sh.parse(GetDefaultResources(),100,false,messages);
    if(ok){prog.addShader(&sh);ok=prog.link(messages)&&prog.mapIO();}
    if(!ok)throw std::runtime_error(std::string(sh.getInfoLog())+"\n"+prog.getInfoLog()+"\n"+hlsl);
    std::vector<uint32_t> spv;
    glslang::GlslangToSpv(*prog.getIntermediate(stage),spv);
    if(spv.size()<5||spv[0]!=0x07230203u)throw std::runtime_error("Invalid SPIR-V header");
    return spv;
}
}
