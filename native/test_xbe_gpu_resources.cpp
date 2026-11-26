#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "gpu/guest_memory.h"
#include <cassert>
#include <cstring>
#include <stdexcept>
int main() {
    hulkpocket::XboxGuestAddressSpace mem;
    mem.Commit(0x10000,0x10000);
    mem.Commit(0x80100000,0x10000);
    gpu::ScopedGuestMemory scope(mem);
    // 4x2 Morton layout: x bits occupy 0 and 2; y occupies bit 1.
    const uint8_t swizzled[]={0,1,4,5,2,3,6,7};
    mem.Write(0x80100000,swizzled,sizeof(swizzled));
    gpu::TextureDesc d{};
    d.data=0x80100000;d.width=4;d.height=2;d.format=0;
    std::vector<uint8_t> pixels;uint32_t pitch=0;
    gpu::DecodeGuestLevel(d,0,0,0,pixels,pitch);
    assert(pitch==16 && pixels.size()==32);
    for(unsigned i=0;i<8;++i)assert(pixels[4*i]==i && pixels[4*i+3]==255);
    // Single-byte texture at last byte of a committed region.
    d.data=0x8010ffff;d.width=1;d.height=1;
    uint8_t gray=77;mem.Write(d.data,&gray,1);
    gpu::DecodeGuestLevel(d,0,0,0,pixels,pitch);
    assert(pixels.size()==4 && pixels[0]==77);
    // D16 at end of region must not read a nonexistent DWORD.
    d.data=0x8010fffe;d.format=0x2c;
    uint16_t depth=65535;mem.Write(d.data,&depth,2);
    gpu::DecodeGuestLevel(d,0,0,0,pixels,pitch);
    float value=0;std::memcpy(&value,pixels.data(),4);assert(value==1.0f);
    // DXT1 block stays compressed.
    d.data=0x80100100;d.format=0x0c;d.width=4;d.height=4;
    const uint8_t block[]={1,2,3,4,5,6,7,8};mem.Write(d.data,block,8);
    gpu::DecodeGuestLevel(d,0,0,0,pixels,pitch);
    assert(pitch==8 && pixels.size()==8 && std::memcmp(pixels.data(),block,8)==0);
    // Xbox D3DPixelContainer: physical resource data and 4x4 DXT1.
    mem.Write32(0x10000,0x40000);mem.Write32(0x10004,0x100100);
    mem.Write32(0x1000c,0x02210c20);mem.Write32(0x10010,0);
    gpu::TextureDesc parsed{};assert(gpu::Describe(0x10000,parsed));
    assert(parsed.data==0x80100100 && parsed.width==4 && parsed.height==4 && parsed.format==0x0c);
    bool rejected=false;d.data=0x8010ffff;
    try {gpu::DecodeGuestLevel(d,0,0,0,pixels,pitch);}catch(const std::out_of_range&){rejected=true;}assert(rejected);
    // FVF XYZ + diffuse + one texture coordinate set.
    gpu::VertexShader layout{};gpu::FvfInputs(0x142,layout);
    assert(layout.stream_stride[0]==24 && layout.inputs[3].offset==12 && layout.inputs[9].offset==16);
    gpu::State state{};state.vs_handle=2;state.streams[0]={0x10100,12};
    mem.Write32(0x10104,0x100200);
    const float verts[12]={0,0,0,1,0,0,1,1,0,0,1,0};
    mem.Write(0x80100200,verts,sizeof(verts));
    gpu::Assembled draw{};
    assert(gpu::Assemble(state,5,0,3,nullptr,draw));
    assert(draw.draw_count==3 && draw.buffers[0].stride==12 && draw.vertex_count==3);
    assert(gpu::Assemble(state,8,0,4,nullptr,draw));
    assert((draw.indices==std::vector<uint32_t>{0,1,2,0,2,3}));
    // Invalid buffer never reaches raw-pointer assembly.
    mem.Write32(0x10104,0x10fff8);rejected=false;
    try {gpu::Assemble(state,5,0,3,nullptr,draw);}catch(const std::out_of_range&){rejected=true;}assert(rejected);
}
