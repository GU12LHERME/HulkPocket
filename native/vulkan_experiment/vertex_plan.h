#pragma once
#include "gpu/vertex.h"
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <vector>
namespace hulkpocket::vkexp {
struct InputFormat {VkFormat format;uint32_t bytes;};
inline InputFormat VertexFormat(uint8_t f){
    using namespace gpu;
    switch(f){
    case kVertexR32_FLOAT:return {VK_FORMAT_R32_SFLOAT,4};
    case kVertexR32G32_FLOAT:return {VK_FORMAT_R32G32_SFLOAT,8};
    case kVertexR32G32B32_FLOAT:return {VK_FORMAT_R32G32B32_SFLOAT,12};
    case kVertexR32G32B32A32_FLOAT:return {VK_FORMAT_R32G32B32A32_SFLOAT,16};
    case kVertexB8G8R8A8_UNORM:return {VK_FORMAT_B8G8R8A8_UNORM,4};
    case kVertexR16_SNORM:return {VK_FORMAT_R16_SNORM,2};
    case kVertexR16G16_SNORM:return {VK_FORMAT_R16G16_SNORM,4};
    case kVertexR16G16B16A16_SNORM:return {VK_FORMAT_R16G16B16A16_SNORM,8};
    case kVertexR8_UNORM:return {VK_FORMAT_R8_UNORM,1};
    case kVertexR8G8_UNORM:return {VK_FORMAT_R8G8_UNORM,2};
    case kVertexR8G8B8A8_UNORM:return {VK_FORMAT_R8G8B8A8_UNORM,4};
    case kVertexR16_SINT:return {VK_FORMAT_R16_SINT,2};
    case kVertexR16G16_SINT:return {VK_FORMAT_R16G16_SINT,4};
    case kVertexR16G16B16A16_SINT:return {VK_FORMAT_R16G16B16A16_SINT,8};
    case kVertexR32_UINT:return {VK_FORMAT_R32_UINT,4};
    default:throw std::invalid_argument("Unsupported Xbox vertex format");
    }
}
inline VkPrimitiveTopology Topology(const gpu::Assembled& a){
    switch(a.topology){
    case gpu::Topology::Points:if(!a.strip)return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;break;
    case gpu::Topology::Lines:return a.strip?VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case gpu::Topology::Triangles:return a.strip?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
    throw std::invalid_argument("Unsupported Xbox primitive topology");
}
struct VertexPlan {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    std::vector<VkDeviceSize> offsets;
    VkDeviceSize bytes=0;
};
inline VertexPlan PlanVertices(const gpu::Assembled& a,const VkPhysicalDeviceLimits& limits){
    VertexPlan p;
    if(!a.vertex_count||!a.buffer_count||a.buffer_count>16||a.buffer_count>limits.maxVertexInputBindings)
        throw std::invalid_argument("Invalid Xbox stream count");
    for(uint32_t i=0;i<a.buffer_count;++i){
        auto& b=a.buffers[i];
        if(!b.src||!b.stride||b.stride>limits.maxVertexInputBindingStride||
           uint64_t(b.stride)*a.vertex_count>b.bytes)
            throw std::invalid_argument("Invalid Xbox vertex stream range");
        p.bytes=(p.bytes+3)&~VkDeviceSize(3);p.offsets.push_back(p.bytes);p.bytes+=b.bytes;
        if(p.bytes>64ull*1024*1024)throw std::invalid_argument("Vertex upload exceeds cap");
        p.bindings.push_back({i,b.stride,VK_VERTEX_INPUT_RATE_VERTEX});
    }
    for(uint32_t i=0;i<16;++i)if(a.regs&(1u<<i)){
        auto& e=a.elements[i];auto f=VertexFormat(e.format);
        if(i>=limits.maxVertexInputAttributes||e.buffer>=a.buffer_count||e.offset>limits.maxVertexInputAttributeOffset||
           uint32_t(e.offset)+f.bytes>a.buffers[e.buffer].stride)
            throw std::invalid_argument("Invalid Xbox vertex attribute");
        p.attributes.push_back({i,e.buffer,f.format,e.offset});
    }
    if(p.attributes.empty())throw std::invalid_argument("Empty Xbox vertex declaration");
    return p;
}
}
