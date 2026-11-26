#include "vulkan_bridge_backend.h"
#include "../xbe_d3d8_bridge.h"
using namespace hulkpocket::vkexp;
namespace {
constexpr uint32_t W=64,H=64;
using Fixture=VulkanCore;
void Expect(const char* label,std::array<uint8_t,4> got,std::array<int,4> want){
    std::printf("%s: RGBA %u %u %u %u\n",label,got[0],got[1],got[2],got[3]);
    for(unsigned i=0;i<4;++i)if(std::abs(int(got[i])-want[i])>1)throw std::runtime_error(std::string(label)+" pixel mismatch");
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        { Fixture f;f.Init();
        f.Draw(argv[1],false,false,false);Expect("diffuse center",f.Pixel(32,32),{0,255,0,255});Expect("outside triangle",f.Pixel(0,0),{0,0,0,255});
        f.Draw(argv[1],false,false,true);Expect("alpha blend",f.Pixel(32,32),{128,0,127,191});
        f.Draw(argv[1],true,true,false);Expect("packed directional modulate",f.Pixel(32,32),{0,0,64,255});
        f.Draw(argv[1],false,true,false,true);Expect("two-stage indexed modulation",f.Pixel(32,32),{64,255,16,255});
        using namespace gpu;
        State state{};state.simple[kMColorWriteEnable/4]=0x01010101;
        f.Draw(argv[1],false,false,false,false,&state,true);Expect("overdraw control",f.Pixel(32,32),{255,0,0,255});
        state.rs[kRsZEnable]=1;state.simple[kMZWriteEnable/4]=1;state.simple[kMZFunc/4]=0x201;
        f.Draw(argv[1],false,false,false,false,&state,true);Expect("Xbox depth occlusion",f.Pixel(32,32),{0,255,0,255});
        state.simple[kMZWriteEnable/4]=0;
        f.Draw(argv[1],false,false,false,false,&state,true);Expect("Xbox depth write disabled",f.Pixel(32,32),{255,0,0,255});
        state.rs[kRsZEnable]=0;state.rs[kRsStencilEnable]=1;state.rs[kRsStencilFail]=0x1e00;
        state.simple[kMStencilFunc/4]=0x202;state.simple[kMStencilMask/4]=255;state.simple[kMStencilWriteMask/4]=255;
        state.simple[kMStencilPass/4]=0x1e02;state.simple[kMStencilZFail/4]=0x1e00;
        f.Draw(argv[1],false,false,false,false,&state,true);Expect("Xbox stencil increment and reject",f.Pixel(32,32),{0,255,0,255});
        state.simple[kMStencilWriteMask/4]=0;
        f.Draw(argv[1],false,false,false,false,&state,true);Expect("Xbox stencil write mask",f.Pixel(32,32),{255,0,0,255});
        state.rs[kRsStencilEnable]=0;state.simple[kMColorWriteEnable/4]=0x10000;
        f.Draw(argv[1],false,false,false,false,&state);Expect("Xbox color write mask",f.Pixel(32,32),{0,0,0,255});
        state.rs[kRsZEnable]=2;bool rejected=false;
        try{(void)hulkpocket::vkexp::TranslateRaster(state);}catch(const std::invalid_argument&){rejected=true;}
        if(!rejected)throw std::runtime_error("Unsupported W-buffer silently accepted");
        // End-to-end synthetic Xbox resource headers and committed memory.
        hulkpocket::XboxGuestAddressSpace memory;
        memory.Commit(0x10000,0x10000);memory.Commit(0x80100000,0x10000);
        ScopedGuestMemory scope(memory);
        constexpr uint32_t tex=0x10000,vb=0x10100,ix=0x10200,rt=0x10300;
        memory.Write32(tex,0x40000);memory.Write32(tex+4,0x100000);memory.Write32(tex+12,0x01110620);
        const uint32_t pixels[]={0xff4080ff,0xff4080ff,0xff4080ff,0xff4080ff};
        memory.Write(0x80100000,pixels,sizeof(pixels));
        memory.Write32(vb+4,0x100100);
        const Vertex guest_vertices[]={{-.75f,-.75f,.5f,0xffffffff,0,1},{.75f,-.75f,.5f,0xffffffff,1,1},{0,.75f,.5f,0xffffffff,.5f,0}};
        memory.Write(0x80100100,guest_vertices,sizeof(guest_vertices));
        const uint16_t guest_indices[]={2,0,1};memory.Write(ix,guest_indices,sizeof(guest_indices));
        State guest_state{};guest_state.vs_handle=0x142;guest_state.streams[0]={vb,sizeof(Vertex)};
        guest_state.simple[kMColorWriteEnable/4]=0x01010101;
        Assembled assembled{};
        if(!hulkpocket::vkexp::GuestDraw(guest_state,5,0,3,ix,assembled))throw std::runtime_error("Guest assembly failed");
        f.UploadGuestTexture(tex);
        f.Draw(argv[1],false,true,false,false,&guest_state,false,&assembled);
        Expect("guest BGRA texture and indexed geometry",f.Pixel(32,32),{64,128,255,255});
        // Replace the same resource with a DXT1 red block, exercising
        // decoding and staging upload again, not a constant GPU clear.
        memory.Write32(tex+12,0x01110c20);
        const uint8_t bc[]={0x00,0xf8,0,0,0,0,0,0};memory.Write(0x80100000,bc,sizeof(bc));
        f.UploadGuestTexture(tex);
        f.Draw(argv[1],false,true,false,false,&guest_state,false,&assembled);
        Expect("guest DXT1 sampled texture",f.Pixel(32,32),{255,0,0,255});
        memory.Write32(rt,0x50000);memory.Write32(rt+4,0x102000);
        memory.Write32(rt+12,0x00011221);memory.Write32(rt+16,63|(63<<12)|(3<<24));
        Surface surface{};surface.header=rt;
        hulkpocket::vkexp::StoreColor(surface,static_cast<const uint8_t*>(f.readback.data),W,H);
        if(memory.Read32(0x80102000+(32*W+32)*4)!=0xffff0000u||
           memory.Read32(0x80102000)!=0xff000000u)throw std::runtime_error("Guest BGRA readback mismatch");
        // Exercise rebuilt 32-bit quad indices and a different texture extent.
        const Vertex quad[]={{-.75f,-.75f,.5f,0xffffffff,0,1},{.75f,-.75f,.5f,0xffffffff,1,1},
                             {.75f,.75f,.5f,0xffffffff,1,0},{-.75f,.75f,.5f,0xffffffff,0,0}};
        memory.Write(0x80100100,quad,sizeof(quad));
        if(!hulkpocket::vkexp::GuestDraw(guest_state,8,0,4,0,assembled)||assembled.index16||assembled.index_count!=6)
            throw std::runtime_error("Xbox quad assembly failed");
        memory.Write32(tex+12,0x02210c20); // 4x4 DXT1, same red block
        f.UploadGuestTexture(tex);
        f.Draw(argv[1],false,true,false,false,&guest_state,false,&assembled);
        Expect("guest quad 32-bit indices and resized texture",f.Pixel(48,16),{255,0,0,255});
        // Independent texture stage descriptors, no alias to stage zero.
        memory.Write32(tex+12,0x01110620);
        const uint32_t second_pixels[]={0xff80ffff,0xff80ffff,0xff80ffff,0xff80ffff};
        memory.Write(0x80100000,second_pixels,sizeof(second_pixels));
        f.UploadGuestTexture(tex,1);
        f.Draw(argv[1],false,true,false,true,&guest_state,false,&assembled);
        Expect("independent Xbox texture stages",f.Pixel(32,32),{128,0,0,255});
        // No precompiled variant has this 16-byte position/diffuse layout.
        struct Compact{float x,y,z;uint32_t rgba;};
        const Compact compact[]={{-.75f,-.75f,.5f,0x80ff00ff},{.75f,-.75f,.5f,0x80ff00ff},{0,.75f,.5f,0x80ff00ff}};
        memory.Write(0x80100100,compact,sizeof(compact));
        guest_state.vs_handle=0x42;guest_state.streams[0].stride=sizeof(Compact);
        for(unsigned slot:{2u,3u,256u,16u,17u,18u,19u})for(unsigned i=0;i<4;++i)guest_state.transforms[slot][i*5]=1;
        guest_state.tss[0][kTssColorOp]=guest_state.tss[0][kTssAlphaOp]=1;
        if(!hulkpocket::vkexp::GuestDraw(guest_state,5,0,3,0,assembled))
            throw std::runtime_error("Compact guest declaration failed");
        auto before=f.shaders.Compiled();
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("runtime compiled compact declaration",f.Pixel(32,32),{255,0,255,128});
        if(f.shaders.Compiled()!=before+2)throw std::runtime_error("Runtime shader compilation missing");
        auto hits=f.shaders.Hits();
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        if(f.shaders.Compiled()!=before+2||f.shaders.Hits()!=hits+2)throw std::runtime_error("Runtime shader cache miss");
        guest_state.simple[kMAlphaTestEnable/4]=1;guest_state.simple[kMAlphaFunc/4]=0x204;
        guest_state.simple[kMAlphaRef/4]=200;
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("runtime alpha test discard",f.Pixel(32,32),{0,0,0,255});
        if(f.shaders.Compiled()!=before+3)throw std::runtime_error("Alpha shader variant missing");
        guest_state.simple[kMAlphaRef/4]=100;
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("runtime alpha threshold update",f.Pixel(32,32),{255,0,255,128});
        if(f.shaders.Compiled()!=before+3)throw std::runtime_error("Uniform change recompiled shader");
        // Separate submissions, changed guest matrix and geometry, same color
        // and depth attachments. Verify preservation and explicit reset.
        guest_state.simple[kMAlphaTestEnable/4]=0;
        guest_state.rs[kRsZEnable]=1;guest_state.simple[kMZFunc/4]=0x201;
        guest_state.simple[kMZWriteEnable/4]=1;
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Compact next[3];std::memcpy(next,compact,sizeof(next));
        for(auto& v:next){v.z=.75f;v.rgba=0xff00ff00;}
        memory.Write(0x80100100,next,sizeof(next));
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true,true);
        Expect("depth retained across submissions",f.Pixel(32,32),{255,0,255,128});
        guest_state.transforms[256][12]=.9f;
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true,true);
        Expect("color retained across submissions",f.Pixel(32,32),{255,0,255,128});
        Expect("guest world translation",f.Pixel(52,32),{0,255,0,255});
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("explicit new frame clears old color",f.Pixel(32,32),{0,0,0,255});
        Expect("translated geometry on new frame",f.Pixel(52,32),{0,255,0,255});
        // Exact format/size words from the first RP6 Vulkan game session.
        // Synthetic colored halves reveal wrong normalized-vs-texel sampling.
        memory.Commit(0x80200000,0x130000);
        memory.Write32(tex+4,0x200000);memory.Write32(tex+12,0x00011e29);memory.Write32(tex+16,0x271df27f);
        std::vector<uint32_t> movie(640*480);
        for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x)movie[y*640+x]=x<320?0x0000ff00:0x00ff0000;
        memory.Write(0x80200000,movie.data(),movie.size()*4);
        guest_state.vs_handle=0x142;guest_state.streams[0].stride=sizeof(Vertex);
        guest_state.transforms[256][12]=0;guest_state.rs[kRsZEnable]=0;
        guest_state.textures[0]=tex;
        auto& stage=guest_state.tss[0];stage[kTssColorOp]=stage[kTssAlphaOp]=4;
        stage[kTssColorArg1]=stage[kTssAlphaArg1]=2;stage[kTssColorArg2]=stage[kTssAlphaArg2]=0;
        stage[kTssResultArg]=1;stage[kTssAddressU]=stage[kTssAddressV]=3;
        stage[kTssMinFilter]=stage[kTssMagFilter]=2;
        const Vertex screen[]={{-.75f,-.75f,.5f,0xffffffff,0,480},{.75f,-.75f,.5f,0xffffffff,640,480},
                               {.75f,.75f,.5f,0xffffffff,640,0},{-.75f,.75f,.5f,0xffffffff,0,0}};
        memory.Write(0x80100100,screen,sizeof(screen));
        if(!GuestDraw(guest_state,8,0,4,0,assembled))throw std::runtime_error("Linear movie assembly failed");
        f.UploadGuestTexture(tex);f.SetSampler(0,guest_state);
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("RP6 linear XRGB left",f.Pixel(16,32),{0,255,0,255});
        Expect("RP6 linear XRGB right",f.Pixel(48,32),{255,0,0,255});
        std::fill(movie.begin(),movie.end(),0x000000ff);
        memory.Write(0x80200000,movie.data(),movie.size()*4);
        f.UploadGuestTexture(tex);f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("updated movie same address",f.Pixel(16,32),{0,0,255,255});
        // Guest swizzled 16x16 chain, each lower level red, base green.
        memory.Write32(tex+4,0x200000);memory.Write32(tex+12,0x04450620);memory.Write32(tex+16,0);
        std::vector<uint32_t> mips(256+64+16+4+1,0xffff0000);
        std::fill_n(mips.begin(),256,0xff00ff00);memory.Write(0x80200000,mips.data(),mips.size()*4);
        const Vertex repeat[]={{-.75f,-.75f,.5f,0xffffffff,0,32},{.75f,-.75f,.5f,0xffffffff,32,32},
                               {.75f,.75f,.5f,0xffffffff,32,0},{-.75f,.75f,.5f,0xffffffff,0,0}};
        memory.Write(0x80100100,repeat,sizeof(repeat));
        if(!GuestDraw(guest_state,8,0,4,0,assembled))throw std::runtime_error("Mip geometry failed");
        f.UploadGuestTexture(tex);stage[kTssMipFilter]=0;f.SetSampler(0,guest_state);
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("guest mip disabled",f.Pixel(32,32),{0,255,0,255});
        stage[kTssMipFilter]=1;f.SetSampler(0,guest_state);
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("guest mip minification",f.Pixel(32,32),{255,0,0,255});
        // Fixed-function packed Xbox normal declaration, actual guest material and light.
        guest_state.textures[0]=0;stage[kTssColorOp]=stage[kTssAlphaOp]=1;
        guest_state.vs_handle=1;guest_state.vs=std::make_shared<gpu::VertexShader>();
        guest_state.vs->inputs[0]={0,0,0x32};guest_state.vs->inputs[2]={0,12,0x16};
        guest_state.vs->inputs[9]={0,16,0x22};guest_state.vs->stream_stride[0]=sizeof(Vertex);
        const Vertex lit[]={{-.75f,-.75f,.5f,511u<<22,0,1},{.75f,-.75f,.5f,511u<<22,1,1},{0,.75f,.5f,511u<<22,.5f,0}};
        memory.Write(0x80100100,lit,sizeof(lit));
        guest_state.rs[kRsLighting]=1;guest_state.light_enable=1;
        guest_state.material[0]=guest_state.material[3]=1;
        auto& light=guest_state.lights[0];light[0]=3;light[1]=std::bit_cast<uint32_t>(1.f);light[18]=std::bit_cast<uint32_t>(-1.f);
        if(!GuestDraw(guest_state,5,0,3,0,assembled))throw std::runtime_error("Packed lighting assembly failed");
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("guest directional light",f.Pixel(32,32),{255,0,0,255});
        light[18]=std::bit_cast<uint32_t>(1.f);
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("guest reversed light",f.Pixel(32,32),{0,0,0,255});
        // Synthetic Xbox vertex microcode: mov oPos,v0; mov oD0,c[96].
        // Position is already in guest screen units, including depth scale.
        guest_state.rs[kRsLighting]=0;guest_state.light_enable=0;
        guest_state.vs->inputs.fill({});guest_state.vs->inputs[0]={0,0,0x42};
        guest_state.vs->stream_stride[0]=16;guest_state.streams[0].stride=16;
        guest_state.vs->program_type=0x2078;
        guest_state.vs->program={0,(1u<<21)|0x1b,2u<<26,0xf800,
                                 0,(1u<<21)|(96u<<13)|0x1b,3u<<26,0xf819};
        const float sv[3][4]={{8,56,.5f,1},{56,56,.5f,1},{32,8,.5f,1}};
        memory.Write(0x80100100,sv,sizeof(sv));guest_state.zscale=1;
        guest_state.vsc[96]={.25f,1.f,0.f,1.f};
        if(!GuestDraw(guest_state,5,0,3,0,assembled))throw std::runtime_error("Vertex program assembly failed");
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("programmable vertex constant",f.Pixel(32,32),{64,255,0,255});
        auto shader_count=f.shaders.Compiled();
        guest_state.vsc[96]={0.f,.25f,1.f,1.f};
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("updated vertex constant",f.Pixel(32,32),{0,64,255,255});
        if(f.shaders.Compiled()!=shader_count)throw std::runtime_error("Vertex constant update recompiled shader");
        // Final Xbox combiner: D = C0, G = C1.a. Independent pixel buffer.
        guest_state.ps=std::make_shared<gpu::PixelShader>();
        guest_state.ps->def[8]=0x00000001;guest_state.ps->def[9]=0x00001200;
        guest_state.simple[kMPsFinalConstant0/4]=0xff804020;
        guest_state.simple[kMPsFinalConstant1/4]=0x80000000;
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("programmable final combiner",f.Pixel(32,32),{128,64,32,128});
        guest_state.simple[kMPsFinalConstant0/4]=0xff20ff40;
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("updated pixel constant",f.Pixel(32,32),{32,255,64,128});
        // Reconstruct the very same shader from NV2A writes, including
        // explicit zero stage mode; an incomplete register set must fail.
        hulkpocket::Nv2aConstantCursor cursor{};
        std::vector<hulkpocket::Nv2aMethod> words={{kMPsCombinerCount,0},{kMPsFinalAbcd,1},
            {kMPsFinalEfgh,0x1200},{kMPsFinalConstant0,0xff20ff40},{kMPsFinalConstant1,0x80000000},{0x1e70,0}};
        auto partial=words;partial.pop_back();
        hulkpocket::ApplyNv2aState(guest_state,cursor,partial,true);
        bool rejected_partial=false;
        try{(void)hulkpocket::CaptureNv2aCombiner(guest_state,cursor,0x10000);}catch(const std::logic_error&){rejected_partial=true;}
        if(!rejected_partial)throw std::runtime_error("Incomplete NV2A shader accepted");
        hulkpocket::ApplyNv2aState(guest_state,cursor,{{0x1e70,0}},true);
        guest_state.ps=hulkpocket::CaptureNv2aCombiner(guest_state,cursor,0x10000);
        guest_state.rs[kRsPsTextureModes]=guest_state.ps->def[54];
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("captured NV2A pixel combiner",f.Pixel(32,32),{32,255,64,128});
        guest_state.ps.reset();
        bool bad=false;
        try{hulkpocket::vkexp::GuestDraw(guest_state,5,0,3,0x1ffff,assembled);}catch(const std::out_of_range&){bad=true;}
        if(!bad)throw std::runtime_error("Uncommitted index span accepted");
        memory.Write32(tex+12,0x01110620);memory.Write32(tex+16,0);memory.Write32(tex+4,0x10ffff);bad=false;
        try{f.UploadGuestTexture(tex);}catch(const std::out_of_range&){bad=true;}
        if(!bad)throw std::runtime_error("Uncommitted texture span accepted");
        }
        // Invoke the same Xbox D3D8 ABI bridge that drives the GLES runtime.
        // No direct fixture calls for this sequence.
        {
            hulkpocket::XboxGuestAddressSpace bridge_mem;
            bridge_mem.Commit(0x20000,0x10000);bridge_mem.Commit(0x80110000,0x10000);
            VulkanBridgeBackend backend;hulkpocket::XbeD3d8Bridge bridge(bridge_mem,backend);
            bridge.Initialize(64,64);
            constexpr uint32_t surface=0x20100,buffer=0x20200;
            bridge_mem.Write32(surface,0x50000);bridge_mem.Write32(surface+4,0x110000);
            bridge_mem.Write32(surface+12,0x00011221);bridge_mem.Write32(surface+16,63|(63<<12)|(3<<24));
            bridge_mem.Write32(buffer,0x40000);bridge_mem.Write32(buffer+4,0x114000);
            struct Compact{float x,y,z;uint32_t rgba;};
            const Compact compact[]={{-.75f,-.75f,.5f,0x80ff00ff},{.75f,-.75f,.5f,0x80ff00ff},{0,.75f,.5f,0x80ff00ff}};
            bridge_mem.Write(0x80114000,compact,sizeof(compact));
            X86Context cpu{};
            auto call=[&](std::string_view name,std::initializer_list<uint32_t> args){
                cpu.esp=0x2f000;bridge_mem.Write32(cpu.esp,0x20000);
                unsigned n=0;for(auto v:args)bridge_mem.Write32(cpu.esp+4+4*n++,v);
                bridge.Call(name,cpu,hulkpocket::D3dAbi::Stdcall(unsigned(args.size())));
                if(cpu.eax||cpu.esp!=0x2f004+4*args.size())throw std::runtime_error("D3D8 ABI stack mismatch");
            };
            call("D3DDevice_SetRenderTarget",{surface,0});
            call("D3DDevice_Clear",{0,0,0xf0,0xff0000ff,std::bit_cast<uint32_t>(1.f),0});
            call("D3DDevice_SetStreamSource",{0,buffer,sizeof(Compact)});
            call("D3DDevice_SetVertexShader",{0x42});
            call("D3DDevice_DrawVertices",{5,0,3});
            if(bridge.Present(surface)!=1||
               bridge_mem.Read32(0x80110000+(32*64+32)*4)!=0x80ff00ff||
               bridge_mem.Read32(0x80110000)!=0xff0000ff)
                throw std::runtime_error("D3D8 Vulkan ABI frame mismatch");
            // Real swap-style physical backing rotation behind the same header.
            {
                gpu::ScopedGuestMemory scope(bridge_mem);
                bridge_mem.Write32(surface+4,0x118000);
                bridge.BindInitialTargets(surface,0);
                backend.BeginRotatedFrame(bridge.Snapshot().rt);
                call("D3DDevice_Clear",{0,0,0xf0,0xff00ff00,std::bit_cast<uint32_t>(1.f),0});
                if(bridge.Present(surface)!=2||bridge_mem.Read32(0x80118000)!=0xff00ff00)
                    throw std::runtime_error("Vulkan rotated frame mismatch");
                bridge_mem.Write32(surface+4,0x110000);
                bridge.BindInitialTargets(surface,0);
                backend.BeginRotatedFrame(bridge.Snapshot().rt);
                backend.RestoreRotatedColor(bridge.Snapshot().rt);
                if(backend.ReadPixel(32,32)!=std::array<uint8_t,4>{255,0,255,128}||
                   bridge.Present(surface)!=3||bridge_mem.Read32(0x80110000+(32*64+32)*4)!=0x80ff00ff)
                    throw std::runtime_error("Vulkan restored frame mismatch");
            }
            std::puts("PASS D3D8 bridge Clear/Draw/Present -> Vulkan -> Xbox memory and rotation");
        }
        std::puts("PASS Vulkan draws, Xbox depth/stencil, checked guest vertex/index assembly, BGRA and DXT1 staging, guest surface readback");
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
