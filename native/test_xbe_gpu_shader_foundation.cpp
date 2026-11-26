#include "gpu/backend.h"
#include "gpu/shaders.h"
#include <cassert>
#include <string>
int main() {
    gpu::State state{};
    gpu::TexInfo textures[4]{};
    auto fixed=gpu::MakeFixedKey(state);
    auto vertex=gpu::FixedVertexHlsl(1,false,0,fixed);
    auto key=gpu::MakePixelKey(state,textures);
    auto pixel=gpu::PixelHlsl(key);
    assert(!vertex.empty() && vertex.find("SV_Position")!=std::string::npos);
    assert(!pixel.empty() && pixel.find("SV_Target")!=std::string::npos);
    assert(key==gpu::MakePixelKey(state,textures));
    textures[0].kind=gpu::kTex2D;
    auto textured=gpu::MakePixelKey(state,textures);
    assert(!(key==textured));
    assert(!gpu::PixelHlsl(textured).empty());
    assert(!gpu::PointHlsl().empty());
}
