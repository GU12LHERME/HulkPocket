#include "gpu/shaders.h"
#include "texture_stages.h"
#include "hlsl_adapter.h"
#include <cassert>
#include <fstream>
#include <string>
using hulkpocket::upstream_probe::Adapt;
int main(int argc,char** argv){
    assert(argc==2);
    gpu::State s{};
    assert(gpu::SampledTextureStages(s)==0);
    s.tss[0][gpu::kTssColorOp]=4;
    s.tss[1][gpu::kTssColorOp]=4;
    s.tss[2][gpu::kTssColorOp]=1;
    assert(gpu::SampledTextureStages(s)==3);
    assert(!gpu::TextureModeSamples(0)&&!gpu::TextureModeSamples(4)&&gpu::TextureModeSamples(1));
    auto v=Adapt(gpu::FixedVertexHlsl(1,false,0,gpu::MakeFixedKey(s)));
    assert(v.find("[[vk::location(0)]]")!=std::string::npos);
    assert(Adapt("1.#INF").find("asfloat(0x7F800000u)")!=std::string::npos);
    std::ofstream(std::string(argv[1])+"/fixed.vert.hlsl")<<v;
    // Untyped diffuse fixed-function path, with no bound textures.
    gpu::State untextured{};
    gpu::TexInfo textures[4]{};
    auto p=Adapt(gpu::PixelHlsl(gpu::MakePixelKey(untextured,textures)));
    assert(p.find("SV_Target")!=std::string::npos);
    std::ofstream(std::string(argv[1])+"/fixed.frag.hlsl")<<p;
    // Verify a real textured fixed-function variant as well.
    s.tss[1][gpu::kTssColorOp]=1;
    textures[0].kind=gpu::kTex2D;
    auto textured=Adapt(gpu::PixelHlsl(gpu::MakePixelKey(s,textures)));
    assert(textured.find("SV_Target")!=std::string::npos);
    std::ofstream(std::string(argv[1])+"/textured.frag.hlsl")<<textured;
}
