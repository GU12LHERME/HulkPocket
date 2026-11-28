#pragma once
// Translate the Xbox NV2A render-state encodings already consumed by the
// GLES runtime. Reject unsupported values rather than silently drawing.
#include "gpu/state.h"
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <bit>
#include <cmath>
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
inline VkBlendFactor Blend(uint32_t v,bool alpha=false) {
    switch(v){
    case 0:return VK_BLEND_FACTOR_ZERO;
    case 1:return VK_BLEND_FACTOR_ONE;
    case 0x300:return alpha?VK_BLEND_FACTOR_SRC_ALPHA:VK_BLEND_FACTOR_SRC_COLOR;
    case 0x301:return alpha?VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 0x302:return VK_BLEND_FACTOR_SRC_ALPHA;
    case 0x303:return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 0x304:return VK_BLEND_FACTOR_DST_ALPHA;
    case 0x305:return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 0x306:return alpha?VK_BLEND_FACTOR_DST_ALPHA:VK_BLEND_FACTOR_DST_COLOR;
    case 0x307:return alpha?VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 0x308:return alpha?VK_BLEND_FACTOR_ONE:VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case 0x8001:return alpha?VK_BLEND_FACTOR_CONSTANT_ALPHA:VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 0x8002:return alpha?VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case 0x8003:return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case 0x8004:return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
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
inline VkPipelineRasterizationStateCreateInfo TranslateWinding(const gpu::State& s,bool triangles,VkFormat depth_format=VK_FORMAT_D24_UNORM_S8_UINT){
    VkPipelineRasterizationStateCreateInfo r{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    r.polygonMode=VK_POLYGON_MODE_FILL;r.lineWidth=1;r.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
    auto fill=s.rs[gpu::kRsFillMode],back=s.rs[gpu::kRsBackFillMode],cull=s.rs[gpu::kRsCullMode];
    if((fill&&fill!=0x1b02)||(back&&back!=0x1b02))
        throw std::invalid_argument("Unsupported non-solid Xbox polygon mode");
    if(cull!=0&&cull!=0x900&&cull!=0x901)
        throw std::invalid_argument("Unknown Xbox culling mode");
    // Guest winding maps to the same Vulkan winding (established by the
    // device-validated cull mapping: D3DCULL_CW discards Vulkan CW). Set
    // frontFace from D3DRS_FRONTFACE (default CW) so gl_FrontFacing matches
    // D3D front faces for two-sided lighting, and pick FRONT/BACK culling so
    // the discarded winding is unchanged.
    const auto front=s.rs[gpu::kRsFrontFace];
    if(front!=0&&front!=0x900&&front!=0x901)
        throw std::invalid_argument("Unknown Xbox front face");
    r.frontFace=front==0x901?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE;
    if(triangles&&cull){
        const auto culled=cull==0x900?VK_FRONT_FACE_CLOCKWISE:VK_FRONT_FACE_COUNTER_CLOCKWISE;
        r.cullMode=culled==r.frontFace?VK_CULL_MODE_FRONT_BIT:VK_CULL_MODE_BACK_BIT;
    }
    auto offset=s.simple[gpu::kMSolidOffsetEnable/4];
    if(offset>1)throw std::invalid_argument("Invalid Xbox polygon offset enable");
    if(offset&&triangles&&s.rs[gpu::kRsZEnable]){
        float slope=std::bit_cast<float>(s.simple[gpu::kMSlopeScale/4]);
        float units=std::bit_cast<float>(s.simple[gpu::kMDepthOffset/4]);
        if(!std::isfinite(slope)||!std::isfinite(units)||!std::isfinite(s.zscale)||s.zscale<=0)
            throw std::invalid_argument("Invalid Xbox polygon offset");
        // The constant term is expressed in guest depth-buffer units.
        // D32 floating point does not have a fixed 24-bit quantization step.
        if(units&&depth_format!=VK_FORMAT_D24_UNORM_S8_UINT)
            throw std::invalid_argument("Constant Xbox depth offset needs D24 target");
        double factor=double(units)*16777215.0/double(s.zscale);
        if(!std::isfinite(factor)||std::abs(factor)>16777215.0)
            throw std::invalid_argument("Xbox depth offset exceeds supported range");
        r.depthBiasConstantFactor=float(std::round(factor));
        r.depthBiasSlopeFactor=slope;
        r.depthBiasEnable=r.depthBiasConstantFactor!=0||slope!=0;
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
        if(m(kMDestBlend)==0x308)throw std::invalid_argument("Invalid Xbox destination saturation factor");
        cb.srcColorBlendFactor=Blend(m(kMSrcBlend));
        cb.srcAlphaBlendFactor=Blend(m(kMSrcBlend),true);
        cb.dstColorBlendFactor=Blend(m(kMDestBlend));
        cb.dstAlphaBlendFactor=Blend(m(kMDestBlend),true);
        cb.colorBlendOp=cb.alphaBlendOp=BlendOp(m(kMBlendOp));
    }
    return out;
}
}
