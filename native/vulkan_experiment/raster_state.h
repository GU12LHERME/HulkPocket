#pragma once
// Translate the Xbox NV2A render-state encodings already consumed by the
// GLES runtime. Reject unsupported values rather than silently drawing.
#include "gpu/state.h"
#include <vulkan/vulkan.h>
#include <stdexcept>
namespace hulkpocket::vkexp {
inline VkCompareOp Compare(uint32_t v) {
    if(v<0x200||v>0x207)throw std::invalid_argument("Unknown Xbox comparison");
    return static_cast<VkCompareOp>(v-0x200);
}
inline VkStencilOp Stencil(uint32_t v) {
    switch(v){
    case 0:return VK_STENCIL_OP_ZERO;
    case 0x1e00:return VK_STENCIL_OP_KEEP;
    case 0x1e01:return VK_STENCIL_OP_REPLACE;
    case 0x1e02:return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case 0x1e03:return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case 0x150a:return VK_STENCIL_OP_INVERT;
    case 0x8507:return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case 0x8508:return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default:throw std::invalid_argument("Unknown Xbox stencil operation");
    }
}
inline VkBlendFactor Blend(uint32_t v) {
    switch(v){
    case 0:return VK_BLEND_FACTOR_ZERO;
    case 1:return VK_BLEND_FACTOR_ONE;
    case 0x300:return VK_BLEND_FACTOR_SRC_COLOR;
    case 0x301:return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 0x302:return VK_BLEND_FACTOR_SRC_ALPHA;
    case 0x303:return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 0x304:return VK_BLEND_FACTOR_DST_ALPHA;
    case 0x305:return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 0x306:return VK_BLEND_FACTOR_DST_COLOR;
    case 0x307:return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    default:throw std::invalid_argument("Unknown Xbox blend factor");
    }
}
inline VkBlendOp BlendOp(uint32_t v) {
    switch(v){
    case 0x8006:return VK_BLEND_OP_ADD;
    case 0x8007:return VK_BLEND_OP_MIN;
    case 0x8008:return VK_BLEND_OP_MAX;
    case 0x800a:return VK_BLEND_OP_SUBTRACT;
    case 0x800b:return VK_BLEND_OP_REVERSE_SUBTRACT;
    default:throw std::invalid_argument("Unknown Xbox blend operation");
    }
}
// Xbox D3DCULL values describe the winding to discard, not the
// winding to keep. Matches the pinned SCDA Vulkan backend mapping.
inline VkPipelineRasterizationStateCreateInfo TranslateWinding(const gpu::State& s,bool triangles){
    VkPipelineRasterizationStateCreateInfo r{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    r.polygonMode=VK_POLYGON_MODE_FILL;r.lineWidth=1;r.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
    auto fill=s.rs[gpu::kRsFillMode],back=s.rs[gpu::kRsBackFillMode],cull=s.rs[gpu::kRsCullMode];
    if((fill&&fill!=0x1b02)||(back&&back!=0x1b02))
        throw std::invalid_argument("Unsupported non-solid Xbox polygon mode");
    if(cull!=0&&cull!=0x900&&cull!=0x901)
        throw std::invalid_argument("Unknown Xbox culling mode");
    if(triangles&&cull){
        r.cullMode=VK_CULL_MODE_BACK_BIT;
        r.frontFace=cull==0x900?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE;
    }
    return r;
}
struct RasterState {
    VkPipelineDepthStencilStateCreateInfo depth{};
    VkPipelineColorBlendAttachmentState color{};
};
inline RasterState TranslateRaster(const gpu::State& s){
    using namespace gpu;
    auto m=[&](uint32_t method){return s.simple[method/4];};
    auto enabled=[](uint32_t v){if(v>1)throw std::invalid_argument("Unknown Xbox enable value");return v!=0;};
    RasterState out{};
    auto& ds=out.depth;ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable=enabled(s.rs[kRsZEnable]);
    ds.depthWriteEnable=ds.depthTestEnable&&enabled(m(kMZWriteEnable));
    ds.depthCompareOp=ds.depthTestEnable?Compare(m(kMZFunc)):VK_COMPARE_OP_ALWAYS;
    ds.stencilTestEnable=enabled(s.rs[kRsStencilEnable]);
    if(ds.stencilTestEnable){
        VkStencilOpState st{};
        st.failOp=Stencil(s.rs[kRsStencilFail]);st.passOp=Stencil(m(kMStencilPass));
        st.depthFailOp=Stencil(m(kMStencilZFail));st.compareOp=Compare(m(kMStencilFunc));
        st.compareMask=m(kMStencilMask)&255;st.writeMask=m(kMStencilWriteMask)&255;
        st.reference=m(kMStencilRef)&255;ds.front=ds.back=st;
    }
    auto& cb=out.color;
    uint32_t mask=m(kMColorWriteEnable);
    if(mask&~0x01010101u)throw std::invalid_argument("Invalid Xbox color mask");
    if(mask&0x10000)cb.colorWriteMask|=VK_COLOR_COMPONENT_R_BIT;
    if(mask&0x100)cb.colorWriteMask|=VK_COLOR_COMPONENT_G_BIT;
    if(mask&1)cb.colorWriteMask|=VK_COLOR_COMPONENT_B_BIT;
    if(mask&0x1000000)cb.colorWriteMask|=VK_COLOR_COMPONENT_A_BIT;
    cb.blendEnable=enabled(m(kMBlendEnable));
    if(cb.blendEnable){
        cb.srcColorBlendFactor=cb.srcAlphaBlendFactor=Blend(m(kMSrcBlend));
        cb.dstColorBlendFactor=cb.dstAlphaBlendFactor=Blend(m(kMDestBlend));
        cb.colorBlendOp=cb.alphaBlendOp=BlendOp(m(kMBlendOp));
    }
    return out;
}
}
