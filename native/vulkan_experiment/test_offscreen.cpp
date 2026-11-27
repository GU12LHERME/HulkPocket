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
        state.rs[kRsZEnable]=0;state.simple[kMColorWriteEnable/4]=0x01010101;
        state.rs[kRsCullMode]=0x900;f.Draw(argv[1],false,false,false,false,&state);
        auto cw=f.Pixel(32,32);
        state.rs[kRsCullMode]=0x901;f.Draw(argv[1],false,false,false,false,&state);
        auto ccw=f.Pixel(32,32);
        const std::array<uint8_t,4> drawn{0,255,0,255},culled{0,0,0,255};
        if(!((cw==drawn&&ccw==culled)||(cw==culled&&ccw==drawn)))
            throw std::runtime_error("Opposite Xbox windings did not cull complementary faces");
        state.rs[kRsCullMode]=0;
        // Equal-depth overlay is rejected by LESS until a negative Xbox
        // polygon offset moves it slightly toward the camera.
        State bias{};bias.rs[kRsZEnable]=1;
        bias.simple[kMColorWriteEnable/4]=0x01010101;
        bias.simple[kMZWriteEnable/4]=1;bias.simple[kMZFunc/4]=0x201;
        f.Draw(argv[1],false,false,false,false,&bias);
        f.Draw(argv[1],false,false,true,false,&bias,false,nullptr,false,true);
        Expect("equal depth overlay rejected",f.Pixel(32,32),{0,255,0,255});
        bias.simple[kMSolidOffsetEnable/4]=1;
        bias.simple[kMDepthOffset/4]=std::bit_cast<uint32_t>(-1024.f);
        if(f.depth_format==VK_FORMAT_D24_UNORM_S8_UINT){
            f.Draw(argv[1],false,false,true,false,&bias,false,nullptr,false,true);
            Expect("negative polygon offset draws overlay",f.Pixel(32,32),{255,0,0,128});
        }else{
            bool unsupported=false;
            try{(void)TranslateWinding(bias,true,f.depth_format);}catch(const std::invalid_argument&){unsupported=true;}
            if(!unsupported)throw std::runtime_error("Floating-point constant depth bias silently accepted");
        }
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
        auto raw_decodes_before=f.texture_decodes,raw_hits_before=f.raw_texture_hits,uploads_before=f.texture_uploads;
        f.UploadGuestTexture(tex);f.UploadGuestTexture(tex);
        if(f.texture_decodes!=raw_decodes_before||f.raw_texture_hits!=raw_hits_before+2||f.texture_uploads!=uploads_before)
            throw std::runtime_error("Identical texture was decoded again");
        // A same-address paletted texture must be invalidated on palette
        // changes, even when none of the texture indices change.
        memory.Write32(tex+12,0x01110b20);
        const uint8_t palette_indices[]={1,1,1,1};memory.Write(0x80100000,palette_indices,sizeof(palette_indices));
        std::array<uint32_t,256> pal{};pal[1]=0xff00ff00;
        constexpr uint32_t palette_address=0x80104000;
        memory.Write(palette_address,pal.data(),sizeof(pal));
        f.UploadGuestTexture(tex,0,palette_address);
        f.Draw(argv[1],false,true,false,false,&guest_state,false,&assembled);
        Expect("paletted texture initial color",f.Pixel(32,32),{0,255,0,255});
        raw_decodes_before=f.texture_decodes;f.UploadGuestTexture(tex,0,palette_address);
        if(f.texture_decodes!=raw_decodes_before)throw std::runtime_error("Palette cache missed");
        pal[1]=0xff0000ff;memory.Write(palette_address,pal.data(),sizeof(pal));
        f.UploadGuestTexture(tex,0,palette_address);
        f.Draw(argv[1],false,true,false,false,&guest_state,false,&assembled);
        Expect("palette-only mutation",f.Pixel(32,32),{0,0,255,255});
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
        hulkpocket::vkexp::StoreColor(surface,f.ColorPixels(),W,H);
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
        raw_decodes_before=f.texture_decodes;f.UploadGuestTexture(tex);
        if(f.texture_decodes!=raw_decodes_before)throw std::runtime_error("Unchanged mip chain decoded again");
        std::fill(mips.begin()+256,mips.end(),0xff0000ff);
        memory.Write(0x80200000,mips.data(),mips.size()*4);
        f.UploadGuestTexture(tex);
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("lower mip mutation invalidates raw cache",f.Pixel(32,32),{0,0,255,255});
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
        auto pipeline_count=f.pipeline_creates,cache_hits=f.pipeline_hits;
        guest_state.vsc[96]={0.f,.25f,1.f,1.f};
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("updated vertex constant",f.Pixel(32,32),{0,64,255,255});
        if(f.shaders.Compiled()!=shader_count)throw std::runtime_error("Vertex constant update recompiled shader");
        if(f.pipeline_creates!=pipeline_count||f.pipeline_hits!=cache_hits+1)
            throw std::runtime_error("Constant-only draw failed to reuse pipeline");
        guest_state.viewport={0,0,64,64,0,1};
        guest_state.scissor_count=1;guest_state.scissors={32,0,64,64};
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true,true,true);
        if(f.pipeline_creates!=pipeline_count)throw std::runtime_error("Dynamic scissor recreated pipeline");
        guest_state.scissor_count=0;
        // Final Xbox combiner: D = C0, G = C1.a. Independent pixel buffer.
        guest_state.ps=std::make_shared<gpu::PixelShader>();
        guest_state.ps->def[8]=0x00000001;guest_state.ps->def[9]=0x00001200;
        guest_state.simple[kMPsFinalConstant0/4]=0xff804020;
        guest_state.simple[kMPsFinalConstant1/4]=0x80000000;
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("programmable final combiner",f.Pixel(32,32),{128,64,32,128});
        pipeline_count=f.pipeline_creates;
        guest_state.simple[kMPsFinalConstant0/4]=0xff20ff40;
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        if(f.pipeline_creates!=pipeline_count)throw std::runtime_error("Pixel constant update recreated pipeline");
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
        // Upload the actual synthetic microcode through the NV2A window,
        // split across packets, starting at a nonzero instruction slot.
        const auto original=guest_state.vs->program;
        hulkpocket::ApplyNv2aState(guest_state,cursor,{{0x1e94,2},{0x1e9c,3},{0x1ea0,3}},true);
        for(unsigned i=0;i<4;++i)hulkpocket::ApplyNv2aState(guest_state,cursor,{{0xb00+i*4,original[i]}},true);
        bool short_program=false;
        try{(void)hulkpocket::CaptureNv2aVertexProgram(cursor,2);}catch(const std::logic_error&){short_program=true;}
        if(!short_program)throw std::runtime_error("Incomplete vertex upload accepted");
        for(unsigned i=4;i<8;++i)hulkpocket::ApplyNv2aState(guest_state,cursor,{{0xb00+(i-4)*4,original[i]}},true);
        guest_state.vs->program=hulkpocket::CaptureNv2aVertexProgram(cursor,2);
        if(guest_state.vs->program!=original)throw std::runtime_error("NV2A vertex upload differs");
        f.Draw(argv[1],false,false,false,false,&guest_state,false,&assembled,true);
        Expect("captured NV2A vertex program",f.Pixel(32,32),{0,64,255,255});
        hulkpocket::ApplyNv2aState(guest_state,cursor,{{0x1e94,0}},true);
        bool wrong_mode=false;
        try{(void)hulkpocket::CaptureNv2aVertexProgram(cursor,2);}catch(const std::logic_error&){wrong_mode=true;}
        if(!wrong_mode)throw std::runtime_error("Fixed mode accepted as programmable");
        bool bad=false;
        try{hulkpocket::vkexp::GuestDraw(guest_state,5,0,3,0x1ffff,assembled);}catch(const std::out_of_range&){bad=true;}
        if(!bad)throw std::runtime_error("Uncommitted index span accepted");
        memory.Write32(tex+12,0x01110620);memory.Write32(tex+16,0);memory.Write32(tex+4,0x10ffff);bad=false;
        try{f.UploadGuestTexture(tex);}catch(const std::out_of_range&){bad=true;}
        if(!bad)throw std::runtime_error("Uncommitted texture span accepted");
        // Vary stencil reference (baked state) to exceed the cache bound.
        // The synchronous fixture must evict old pipelines without leaking.
        State cache_state{};cache_state.simple[kMColorWriteEnable/4]=0x01010101;
        cache_state.rs[kRsStencilEnable]=1;cache_state.rs[kRsStencilFail]=0x1e00;
        cache_state.simple[kMStencilFunc/4]=0x207;cache_state.simple[kMStencilPass/4]=0x1e00;
        cache_state.simple[kMStencilZFail/4]=0x1e00;
        for(unsigned i=0;i<VulkanCore::PipelineLimit+2;++i){
            cache_state.simple[kMStencilRef/4]=i;
            f.Draw(argv[1],false,false,false,false,&cache_state);
        }
        if(f.pipelines.size()!=VulkanCore::PipelineLimit)throw std::runtime_error("Vulkan pipeline cache not bounded");
        Expect("pipeline cache eviction",f.Pixel(32,32),{0,255,0,255});
        // Warm both pipeline variants after eviction stress above. A cache
        // miss at the capacity limit legitimately flushes pending work.
        // Isolate the readback batching test from that eviction behavior.
        f.Draw(argv[1],false,false,false);
        f.Draw(argv[1],false,false,true);
        f.FlushBatch();
        // Multiple submissions may update color without forcing host transfers.
        auto copies=f.readback_copies,readback_hits_before=f.readback_hits,draws=f.draw_submissions;
        f.ClearFrame(0xf3,0xff0000ff,1,0);
        f.Draw(argv[1],false,false,false);
        f.Draw(argv[1],false,false,true);
        if(f.readback_copies!=copies||f.draw_submissions!=draws+2)
            throw std::runtime_error("Draw or clear forced eager readback");
        auto readback_submits=f.queue_submissions,readback_batches=f.draw_batches;
        if(f.pending_draws!=2)throw std::runtime_error("Missing pending draws before readback");
        Expect("deferred readback newest color",f.Pixel(32,32),{128,0,127,191});
        if(f.readback_copies!=copies+1)throw std::runtime_error("Missing deferred color transfer");
        if(f.queue_submissions!=readback_submits+1||f.draw_batches!=readback_batches+1||f.pending_draws)
            throw std::runtime_error("Readback failed to coalesce with pending draws");
        Expect("cached readback same frame",f.Pixel(0,0),{0,0,255,255});
        f.ClearFrame(3,0,1,0);
        Expect("depth clear retains CPU color",f.Pixel(32,32),{128,0,127,191});
        if(f.readback_copies!=copies+1||f.readback_hits!=readback_hits_before+2)
            throw std::runtime_error("Repeated color queries or depth clear transferred color");
        f.ClearFrame(0xf0,0xff123456,1,0);
        Expect("clear invalidates cached readback",f.Pixel(32,32),{18,52,86,255});
        if(f.readback_copies!=copies+2)throw std::runtime_error("Color clear did not invalidate readback");
        // Distinct per-draw vertex and constant storage, one submission
        // for multiple ordered draws. Explicit flush does not read color.
        auto submits=f.queue_submissions,batches=f.draw_batches;
        f.Draw(argv[1],false,false,false);
        f.Draw(argv[1],false,false,true,false,nullptr,false,nullptr,false,true);
        if(f.queue_submissions!=submits||f.pending_draws!=2)
            throw std::runtime_error("Vulkan draw batch submitted too early");
        f.FlushBatch();
        if(f.queue_submissions!=submits+1||f.draw_batches!=batches+1||f.pending_draws)
            throw std::runtime_error("Vulkan batch did not submit exactly once");
        Expect("batched distinct vertex colors",f.Pixel(32,32),{128,127,0,191});
        submits=f.queue_submissions;
        for(unsigned i=0;i<VulkanCore::BatchLimit;++i)f.Draw(argv[1],false,false,false);
        if(f.pending_draws||f.queue_submissions!=submits+1)
            throw std::runtime_error("Vulkan batch limit failed");
        Expect("bounded batch final color",f.Pixel(32,32),{0,255,0,255});
        std::printf("Vulkan pipelines created=%llu reused=%llu resident=%zu\n",
            (unsigned long long)f.pipeline_creates,(unsigned long long)f.pipeline_hits,f.pipelines.size());
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
            // Feed two actual Xbox texture headers and a final combiner
            // through the live capture path, not direct fixture bindings.
            {
                using namespace gpu;
                gpu::ScopedGuestMemory scope(bridge_mem);
                constexpr uint32_t device=0x24000,decl=0x27000,ps=0x27200,scratch=0x28000;
                constexpr uint32_t tex0=0x20300,tex1=0x20400;
                constexpr uint32_t format=0x01110629;
                for(unsigned st=0;st<2;++st){
                    auto tex=st?tex1:tex0,physical=0x115000+st*0x100;
                    bridge_mem.Write32(tex,0x40000);bridge_mem.Write32(tex+4,physical);bridge_mem.Write32(tex+12,format);
                    uint32_t colors[4];std::fill_n(colors,4,st?0xff80ffff:0xffff8040);
                    bridge_mem.Write(0x80000000|physical,colors,sizeof(colors));
                    bridge_mem.Write32(device+0xf88+4*st,tex);
                }
                bridge_mem.Write32(device+0x794,decl);bridge_mem.Write32(device+0x784,ps);
                bridge_mem.Write32(decl+4,0x400);
                gpu::VertexShader layout;gpu::FvfInputs(0x142,layout);
                for(unsigned k=0;k<16;++k)if(layout.inputs[k].stream>=0){
                    auto at=decl+0x14+16*k;auto in=layout.inputs[k];
                    bridge_mem.Write32(at,uint32_t(in.stream));bridge_mem.Write32(at+4,in.offset);bridge_mem.Write32(at+8,in.type);
                }
                for(unsigned tr:{0u,1u,6u})for(unsigned k=0;k<4;++k)
                    bridge_mem.Write32(device+0xc60+64*tr+20*k,std::bit_cast<uint32_t>(1.f));
                bridge_mem.Write32(device+0xee8,64);bridge_mem.Write32(device+0xeec,64);
                bridge_mem.Write32(device+0xef4,std::bit_cast<uint32_t>(1.f));
                bridge_mem.Write32(device+0xef8,std::bit_cast<uint32_t>(.53125f));bridge_mem.Write32(device+0xefc,std::bit_cast<uint32_t>(.53125f));
                const Vertex tri[]={{-.75f,-.75f,.5f,0xffffffff,0,1},{.75f,-.75f,.5f,0xffffffff,1,1},{0,.75f,.5f,0xffffffff,.5f,0}};
                bridge_mem.Write(0x80114000,tri,sizeof(tri));call("D3DDevice_SetStreamSource",{0,buffer,sizeof(Vertex)});
                auto state=bridge.Snapshot();
                for(unsigned st=0;st<2;++st){
                    auto& t=state.tss[st];t[kTssAddressU]=t[kTssAddressV]=3;
                    t[kTssMinFilter]=t[kTssMagFilter]=1;
                }
                bridge.AdoptLiveSetters(state);
                std::vector<hulkpocket::Nv2aMethod> packet={{0x1b00,0x115000},{0x1b04,format},
                    {0x1b40,0x115100},{0x1b44,format},{kMPsCombinerCount,0},{kMPsFinalAbcd,0x08090000},
                    {kMPsFinalEfgh,0x1800},{kMPsFinalConstant0,0},{kMPsFinalConstant1,0},{0x1e70,0x21}};
                for(unsigned k=0;k<packet.size();++k){
                    bridge_mem.Write32(scratch+8*k,(1u<<18)|packet[k].method);bridge_mem.Write32(scratch+8*k+4,packet[k].value);
                }
                bridge.ApplyPush(scratch,scratch+8*packet.size(),true);
                if(bridge.SyncFixedDiffuseShader(device,true)!=0x142)throw std::runtime_error("Multi-stage capture declaration mismatch");
                if(bridge.VerifyGuestGeometry(5,0,3,0)!=3)throw std::runtime_error("Multi-stage geometry mismatch");
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("live two-stage captured combiner",backend.ReadPixel(32,32),{128,128,64,255});
                bridge_mem.Write32(tex1+4,0x115200);
                bool mismatched=false;
                try{bridge.SyncFixedDiffuseShader(device,true);}catch(const std::logic_error&){mismatched=true;}
                if(!mismatched)throw std::runtime_error("Mismatched stage one NV2A texture accepted");
                bridge_mem.Write32(tex1+4,0x115100);
                // Move the same geometry into the right half, using actual
                // XDK viewport words. Then restrict it with an API scissor.
                call("D3DDevice_Clear",{0,0,0xf0,0xff0000ff,std::bit_cast<uint32_t>(1.f),0});
                bridge_mem.Write32(device+0xee0,32);bridge_mem.Write32(device+0xee8,32);
                bridge.SyncFixedDiffuseShader(device,true);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("partial viewport center",backend.ReadPixel(48,32),{128,128,64,255});
                Expect("partial viewport outside",backend.ReadPixel(16,32),{0,0,255,255});
                call("D3DDevice_Clear",{0,0,0xf0,0xff0000ff,std::bit_cast<uint32_t>(1.f),0});
                constexpr uint32_t rect=0x27300;
                uint32_t sc[4]={48,0,64,64};bridge_mem.Write(rect,sc,sizeof(sc));
                call("D3DDevice_SetScissors",{1,0,rect});
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("scissor inside",backend.ReadPixel(50,32),{128,128,64,255});
                Expect("scissor excludes covered pixel",backend.ReadPixel(44,32),{0,0,255,255});
                uint32_t clear_rect[4]={48,28,54,36};bridge_mem.Write(rect,clear_rect,sizeof(clear_rect));
                call("D3DDevice_Clear",{1,rect,0xf0,0xff00ff00,std::bit_cast<uint32_t>(1.f),0});
                Expect("rectangle clear inside",backend.ReadPixel(50,32),{0,255,0,255});
                Expect("rectangle clear preserves outside",backend.ReadPixel(44,32),{0,0,255,255});
                // Stencil-only mark followed by a color pass through the
                // marked region, as used in a shadow volume pipeline.
                constexpr uint32_t depth_surface=0x20500;
                bridge_mem.Write32(depth_surface,0x50000);bridge_mem.Write32(depth_surface+4,0x11c000);
                bridge_mem.Write32(depth_surface+12,0x00012e21);bridge_mem.Write32(depth_surface+16,63|(63<<12)|(3<<24));
                call("D3DDevice_SetRenderTarget",{surface,depth_surface});
                call("D3DDevice_SetScissors",{0,0,0});
                bridge_mem.Write32(device+0xee0,0);bridge_mem.Write32(device+0xee8,64);
                bridge.SyncFixedDiffuseShader(device,true);
                call("D3DDevice_Clear",{0,0,0xf3,0xff0000ff,std::bit_cast<uint32_t>(1.f),0});
                auto mask_state=bridge.Snapshot();mask_state.rs[kRsStencilEnable]=1;
                mask_state.rs[kRsStencilFail]=0x1e00;
                mask_state.simple[kMColorWriteEnable/4]=0;
                bridge.AdoptLiveSetters(mask_state);
                std::vector<hulkpocket::Nv2aMethod> stencil_words={{kMStencilFunc,0x207},{kMStencilRef,1},
                    {kMStencilMask,255},{kMStencilWriteMask,255},{kMStencilPass,0x1e01},{kMStencilZFail,0x1e00}};
                auto write_packet=[&](const std::vector<hulkpocket::Nv2aMethod>& p){
                    for(unsigned k=0;k<p.size();++k){
                        bridge_mem.Write32(scratch+8*k,(1u<<18)|p[k].method);bridge_mem.Write32(scratch+8*k+4,p[k].value);
                    }
                    bridge.ApplyPush(scratch,scratch+8*p.size(),true);
                };
                write_packet(stencil_words);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("stencil mark preserves color",backend.ReadPixel(32,32),{0,0,255,255});
                // Larger fullscreen triangle makes the stencil, rather than
                // geometry coverage, responsible for rejecting corner pixels.
                const Vertex large[]={{-1,-1,.5f,0xffffffff,0,1},{3,-1,.5f,0xffffffff,1,1},{-1,3,.5f,0xffffffff,0,0}};
                bridge_mem.Write(0x80114000,large,sizeof(large));
                write_packet({{kMColorWriteEnable,0x01010101},{kMStencilFunc,0x202},{kMStencilWriteMask,0},{kMStencilPass,0x1e00}});
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("stencil masked color inside",backend.ReadPixel(32,32),{128,128,64,255});
                Expect("stencil masked color outside",backend.ReadPixel(4,4),{0,0,255,255});
                // Preserve the shared stencil on a color-only clear after
                // rotation, and restore old color on the next depth-only clear.
                bridge.Present(surface);
                bridge_mem.Write32(surface+4,0x118000);
                bridge.BindInitialTargets(surface,depth_surface);
                backend.BeginRotatedFrame(bridge.Snapshot().rt);
                call("D3DDevice_Clear",{0,0,0xf0,0xff0000ff,std::bit_cast<uint32_t>(1.f),0});
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("rotated color-only clear preserves stencil inside",backend.ReadPixel(32,32),{128,128,64,255});
                Expect("rotated color-only clear preserves stencil outside",backend.ReadPixel(4,4),{0,0,255,255});
                bridge.Present(surface);
                bridge_mem.Write32(surface+4,0x110000);
                bridge.BindInitialTargets(surface,depth_surface);
                backend.BeginRotatedFrame(bridge.Snapshot().rt);
                call("D3DDevice_Clear",{0,0,1,0,std::bit_cast<uint32_t>(1.f),0});
                Expect("rotated depth-only clear retains saved color",backend.ReadPixel(32,32),{128,128,64,255});
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("rotated depth-only clear preserves stencil",backend.ReadPixel(4,4),{0,0,255,255});
                bridge.Present(surface);
                bridge_mem.Write32(surface+4,0x118000);
                bridge.BindInitialTargets(surface,depth_surface);
                backend.BeginRotatedFrame(bridge.Snapshot().rt);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("rotated draw without clear retains stencil",backend.ReadPixel(32,32),{128,128,64,255});
                Expect("rotated draw without clear retains color",backend.ReadPixel(4,4),{0,0,255,255});
                bridge.Present(surface);
                bridge_mem.Write32(surface+4,0x110000);
                bridge.BindInitialTargets(surface,depth_surface);
                backend.BeginRotatedFrame(bridge.Snapshot().rt);
                backend.RestoreRotatedColor(bridge.Snapshot().rt);
                // Capture current render target to a pitched guest surface.
                // Copy a rectangle back into the active image, preserving
                // both untouched pixels and the previous stencil attachment.
                constexpr uint32_t copy_surface=0x20600,copy_rect=0x27400,copy_point=0x27420;
                bridge_mem.Write32(copy_surface,0x50000);bridge_mem.Write32(copy_surface+4,0x116000);
                bridge_mem.Write32(copy_surface+12,0x00011221);bridge_mem.Write32(copy_surface+16,63|(63<<12)|(3<<24));
                call("D3DDevice_CopyRects",{surface,0,0,copy_surface,0});
                if(bridge_mem.Read32(0x80116000+(32*64+32)*4)!=0xff808040||
                   bridge_mem.Read32(0x80116000)!=0xff0000ff)
                    throw std::runtime_error("Vulkan render target copy mismatch");
                uint32_t cr[4]={0,0,8,8},cp[2]={28,28};
                bridge_mem.Write(copy_rect,cr,sizeof(cr));bridge_mem.Write(copy_point,cp,sizeof(cp));
                call("D3DDevice_CopyRects",{copy_surface,copy_rect,1,surface,copy_point});
                Expect("copy into live framebuffer",backend.ReadPixel(32,32),{0,0,255,255});
                Expect("copy preserves nearby color",backend.ReadPixel(40,32),{128,128,64,255});
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("copy preserves stencil",backend.ReadPixel(32,32),{128,128,64,255});
                Expect("copy stencil outside",backend.ReadPixel(4,4),{0,0,255,255});
                // Overlap must read the original source even when writing
                // across its own rows, not cascade newly written pixels.
                bridge_mem.Write32(0x80116000,0xff010203);bridge_mem.Write32(0x80116004,0xff040506);
                uint32_t overlap_rect[4]={0,0,2,1},overlap_point[2]={1,0};
                bridge_mem.Write(copy_rect,overlap_rect,sizeof(overlap_rect));bridge_mem.Write(copy_point,overlap_point,sizeof(overlap_point));
                call("D3DDevice_CopyRects",{copy_surface,copy_rect,1,copy_surface,copy_point});
                if(bridge_mem.Read32(0x80116004)!=0xff010203||bridge_mem.Read32(0x80116008)!=0xff040506)
                    throw std::runtime_error("Overlapping Vulkan copy corrupted source");
                // Active render target as a 64x64 linear texture: GPU
                // snapshot must see newest color, not stale guest memory.
                auto feedback=bridge.Snapshot();
                feedback.rs[kRsStencilEnable]=0;
                feedback.textures={surface,0,0,0};
                feedback.ps=std::make_shared<gpu::PixelShader>();
                feedback.ps->def[8]=8;feedback.ps->def[9]=0x1800;
                feedback.rs[kRsPsTextureModes]=1;
                feedback.simple[kMColorWriteEnable/4]=0x01010101;
                feedback.tss[0][kTssMinFilter]=feedback.tss[0][kTssMagFilter]=1;
                feedback.tss[0][kTssAddressU]=feedback.tss[0][kTssAddressV]=3;
                const Vertex sample[]={{-.75f,-.75f,.5f,0xffffffff,32.5f,32.5f},
                    {.75f,-.75f,.5f,0xffffffff,32.5f,32.5f},{0,.75f,.5f,0xffffffff,32.5f,32.5f}};
                bridge_mem.Write(0x80114000,sample,sizeof(sample));
                call("D3DDevice_Clear",{0,0,0xf0,0xff123456,std::bit_cast<uint32_t>(1.f),0});
                backend.Draw(feedback,5,0,3,nullptr);
                Expect("render target GPU snapshot",backend.ReadPixel(32,32),{18,52,86,255});
                uint32_t source_rect[4]={30,30,34,34};bridge_mem.Write(rect,source_rect,sizeof(source_rect));
                call("D3DDevice_Clear",{1,rect,0xf0,0xffa04020,std::bit_cast<uint32_t>(1.f),0});
                backend.Draw(feedback,5,0,3,nullptr);
                Expect("refreshed target texture snapshot",backend.ReadPixel(32,40),{160,64,32,255});
                Expect("snapshot preserves outside",backend.ReadPixel(0,0),{18,52,86,255});
                // Switching back to an ordinary guest texture must invalidate
                // any CPU cache entries from before the GPU snapshot.
                feedback.textures[0]=tex0;
                const Vertex uv[]={{-.75f,-.75f,.5f,0xffffffff,.5f,.5f},
                    {.75f,-.75f,.5f,0xffffffff,.5f,.5f},{0,.75f,.5f,0xffffffff,.5f,.5f}};
                bridge_mem.Write(0x80114000,uv,sizeof(uv));
                backend.Draw(feedback,5,0,3,nullptr);
                Expect("ordinary texture after framebuffer snapshot",backend.ReadPixel(32,32),{255,128,64,255});
                // Real captured-combiner replay: fog alpha interpolates
                // texture color with the fog register (linear depth 0..1).
                auto fog=bridge.Snapshot();
                fog.rs[kRsStencilEnable]=0;fog.rs[kRsFogEnable]=1;
                fog.rs[kRsFogTableMode]=3;fog.rs[kRsFogStart]=std::bit_cast<uint32_t>(0.f);
                fog.rs[kRsFogEnd]=std::bit_cast<uint32_t>(1.f);
                fog.rs[kRsFogDensity]=std::bit_cast<uint32_t>(1.f);
                fog.rs[kRsFogColor]=0xff0000ff;
                bridge.AdoptLiveSetters(fog);
                write_packet({{kMPsFinalAbcd,0x13080300},{kMPsFinalEfgh,0x1800}});
                bridge.SyncFixedDiffuseShader(device,true);
                call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.f),0});
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("captured linear fog combiner",backend.ReadPixel(32,32),{128,64,160,255});
                fog=bridge.Snapshot();fog.rs[kRsFogEnd]=std::bit_cast<uint32_t>(2.f);
                bridge.AdoptLiveSetters(fog);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("updated fog range",backend.ReadPixel(32,32),{191,96,112,255});
                fog=bridge.Snapshot();fog.rs[kRsFogEnable]=0;bridge.AdoptLiveSetters(fog);
                // 2x2 Morton-order texels: AR of stage zero (1,1) selects
                // yellow bottom-right; GB (.502,.251) selects green top-right.
                uint32_t lookup[4]={0xffff0000,0xff00ff00,0xff0000ff,0xffffff00};
                bridge_mem.Write(0x80115100,lookup,sizeof(lookup));
                write_packet({{0x1e70,1|(0xf<<5)},{kMPsFinalAbcd,9},{kMPsFinalEfgh,0x1900}});
                bridge.SyncFixedDiffuseShader(device,true);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("dependent AR texture lookup",backend.ReadPixel(32,32),{255,255,0,255});
                write_packet({{0x1e70,1|(0x10<<5)}});
                bridge.SyncFixedDiffuseShader(device,true);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("dependent GB texture lookup",backend.ReadPixel(32,32),{0,255,0,255});
                write_packet({{0x1e70,0xf|(1<<5)}});
                bool invalid_dep=false;
                try{bridge.SyncFixedDiffuseShader(device,true);}catch(const std::logic_error&){invalid_dep=true;}
                if(!invalid_dep)throw std::runtime_error("Dependent stage zero silently accepted");
                const Vertex bump_uv[]={{-.75f,-.75f,.5f,0xffffffff,.25f,.25f},
                    {.75f,-.75f,.5f,0xffffffff,.25f,.25f},{0,.75f,.5f,0xffffffff,.25f,.25f}};
                bridge_mem.Write(0x80114000,bump_uv,sizeof(bump_uv));
                write_packet({{0x1e70,1|(6<<5)}});
                bool missing_bump=false;
                try{bridge.SyncFixedDiffuseShader(device,true);}catch(const std::logic_error&){missing_bump=true;}
                if(!missing_bump)throw std::runtime_error("Incomplete bump matrix accepted");
                write_packet({{0x1b68,0},{0x1b6c,0},{0x1b70,0},{0x1b74,0}});
                bridge.SyncFixedDiffuseShader(device,true);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("zero bump offset",backend.ReadPixel(32,32),{255,0,0,255});
                write_packet({{0x1b68,std::bit_cast<uint32_t>(1.f)}});
                bridge.SyncFixedDiffuseShader(device,true);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("bump offset changes sampled texel",backend.ReadPixel(32,32),{0,255,0,255});
                write_packet({{0x1e70,1|(7<<5)},{0x1b78,std::bit_cast<uint32_t>(.5f)},{0x1b7c,0}});
                bridge.SyncFixedDiffuseShader(device,true);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3);
                Expect("bump luminance modulates color",backend.ReadPixel(32,32),{0,128,0,128});
                // Exercise actual bridge replay of converted primitives,
                // rather than only the standalone geometry assembler.
                write_packet({{0x1e70,0x21},{kMPsFinalAbcd,8},{kMPsFinalEfgh,0x1800}});
                bridge.SyncFixedDiffuseShader(device,true);
                const Vertex corners[]={{-.75f,-.75f,.5f,0xffffffff,.25f,.25f},
                    {.75f,-.75f,.5f,0xffffffff,.25f,.25f},{.75f,.75f,.5f,0xffffffff,.25f,.25f},
                    {-.75f,.75f,.5f,0xffffffff,.25f,.25f}};
                bridge_mem.Write(0x80114000,corners,sizeof(corners));
                for(unsigned prim:{7u,8u,10u}){
                    call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.f),0});
                    bridge.ReplayCapturedShaderDiagnostic(prim,0,4);
                    Expect("captured fan/quad/polygon interior",backend.ReadPixel(40,20),{255,128,64,255});
                    Expect("captured primitive outside",backend.ReadPixel(2,2),{0,0,0,255});
                }
                const Vertex strip[]={corners[0],corners[1],corners[3],corners[2]};
                bridge_mem.Write(0x80114000,strip,sizeof(strip));
                call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.f),0});
                bridge.ReplayCapturedShaderDiagnostic(9,0,4);
                Expect("captured quad strip",backend.ReadPixel(40,20),{255,128,64,255});
                bool malformed=false;
                try{bridge.ReplayCapturedShaderDiagnostic(8,0,3);}catch(const std::logic_error&){malformed=true;}
                if(!malformed)throw std::runtime_error("Incomplete quad accepted");
                // Pretransformed XYZRHW declaration uses screen positions
                // rather than the fixed world/view/projection transform.
                struct ScreenVertex{float x,y,z,rhw;uint32_t color;float u,v;};
                const ScreenVertex hud[]={{8,56,.5f,1,0xffffffff,.25f,.25f},
                    {56,56,.5f,1,0xffffffff,.25f,.25f},{56,8,.5f,1,0xffffffff,.25f,.25f},
                    {8,8,.5f,1,0xffffffff,.25f,.25f}};
                gpu::VertexShader screen_layout;gpu::FvfInputs(0x144,screen_layout);
                for(unsigned k=0;k<16;++k)if(screen_layout.inputs[k].stream>=0){
                    auto at=decl+0x14+16*k;auto in=screen_layout.inputs[k];
                    bridge_mem.Write32(at,uint32_t(in.stream));bridge_mem.Write32(at+4,in.offset);bridge_mem.Write32(at+8,in.type);
                }
                bridge_mem.Write(0x80114000,hud,sizeof(hud));
                call("D3DDevice_SetStreamSource",{0,buffer,sizeof(ScreenVertex)});
                if(bridge.SyncFixedDiffuseShader(device,true)!=0x144)throw std::runtime_error("XYZRHW capture mismatch");
                call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.f),0});
                bridge.ReplayCapturedShaderDiagnostic(8,0,4);
                Expect("captured screen-space HUD quad",backend.ReadPixel(32,32),{255,128,64,255});
                Expect("captured HUD outside",backend.ReadPixel(2,2),{0,0,0,255});
                // Same real device declaration and texture bindings, but no
                // pixel program. Exercise the new Vulkan live fixed profile.
                bridge_mem.Write32(device+0x784,0);
                uint32_t fixed_second[4]={0xff204080,0xff204080,0xff204080,0xff204080};
                bridge_mem.Write(0x80115100,fixed_second,sizeof(fixed_second));
                auto fixed_state=bridge.Snapshot();
                fixed_state.rs[kRsFogEnable]=0;fixed_state.rs[kRsRangeFogEnable]=0;
                for(unsigned st=0;st<2;++st){
                    auto& t=fixed_state.tss[st];t[kTssColorOp]=t[kTssAlphaOp]=4;
                    t[kTssColorArg1]=t[kTssAlphaArg1]=st?1:0;
                    t[kTssColorArg2]=t[kTssAlphaArg2]=2;t[kTssResultArg]=1;
                    t[kTssColorSign]=t[kTssColorKeyOp]=t[kTssAlphaKill]=0;
                }
                for(unsigned st=2;st<4;++st)fixed_state.tss[st][kTssColorOp]=fixed_state.tss[st][kTssAlphaOp]=1;
                bridge.AdoptLiveSetters(fixed_state);
                if(bridge.SyncFixedDiffuseShader(device,true)!=0x144)throw std::runtime_error("Fixed multi-stage capture mismatch");
                call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.f),0});
                bridge.ReplayCapturedShaderDiagnostic(8,0,4,0,true);
                Expect("fixed two-stage live HUD",backend.ReadPixel(32,32),{32,32,32,255});
                // Physical RP6 draw had stage 0 active, stage 1 disabled,
                // and stale stage 2 MODULATE. Ignore later stages and their
                // resources without mutating the guest's latent setters.
                const auto two_stage=bridge.Snapshot();
                fixed_state=two_stage;
                fixed_state.tss[1][kTssColorOp]=fixed_state.tss[1][kTssAlphaOp]=1;
                bridge.AdoptLiveSetters(fixed_state);
                call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.f),0});
                bridge.ReplayCapturedShaderDiagnostic(8,0,4,0,true);
                const auto stage_zero=backend.ReadPixel(32,32);
                fixed_state.tss[2][kTssColorOp]=fixed_state.tss[2][kTssAlphaOp]=4;
                fixed_state.tss[2][kTssTexCoordIndex]=3;
                fixed_state.textures[2]=fixed_state.textures[1];
                bridge.AdoptLiveSetters(fixed_state);
                call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.f),0});
                bridge.ReplayCapturedShaderDiagnostic(8,0,4,0,true);
                Expect("disabled fixed stage terminates cascade",backend.ReadPixel(32,32),
                       {stage_zero[0],stage_zero[1],stage_zero[2],stage_zero[3]});
                if(bridge.Snapshot().tss!=fixed_state.tss||bridge.Snapshot().textures!=fixed_state.textures)
                    throw std::runtime_error("Fixed replay mutated latent guest state");
                // Real change of combiner, not a shader-specific replay.
                fixed_state=two_stage;fixed_state.tss[1][kTssColorOp]=7;
                bridge.AdoptLiveSetters(fixed_state);
                bridge.ReplayCapturedShaderDiagnostic(8,0,4,0,true);
                Expect("fixed additive material stage",backend.ReadPixel(32,32),{255,192,192,255});
                fixed_state=bridge.Snapshot();fixed_state.tss[1][kTssColorOp]=17;
                bridge.AdoptLiveSetters(fixed_state);
                bool unsupported_fixed=false;
                try{bridge.ReplayCapturedShaderDiagnostic(8,0,4,0,true);}catch(const std::logic_error&){unsupported_fixed=true;}
                if(!unsupported_fixed)throw std::runtime_error("Unknown fixed operation silently accepted");
                fixed_state.tss[1][kTssColorOp]=4;bridge.AdoptLiveSetters(fixed_state);
                bool legacy_rejected=false;
                try{bridge.ReplayCapturedShaderDiagnostic(8,0,4);}catch(const std::logic_error&){legacy_rejected=true;}
                if(!legacy_rejected)throw std::runtime_error("Fixed extension enabled without opt-in");
                // Switch between two independent 64x64 guest color
                // surfaces using the same depth attachment. The first
                // switch flushes the main target, the second restores it.
                call("D3DDevice_SetRenderTarget",{copy_surface,depth_surface});
                // The original SetRenderTarget emits these NV097 words
                // as well as updating device mirrors. Test both halves.
                write_packet({{0x0208,0x128},{0x020c,0x01000100},{0x0210,0x116000}});
                write_packet({{0x0214,0x11c000}});
                bridge.ValidateCapturedTargets();
                call("D3DDevice_Clear",{0,0,0xf0,0xff102030,std::bit_cast<uint32_t>(1.f),0});
                Expect("secondary target clear",backend.ReadPixel(32,32),{16,32,48,255});
                if(bridge_mem.Read32(0x80110000+(32*64+32)*4)!=0xffffc0c0)
                    throw std::runtime_error("Main target not stored on switch");
                call("D3DDevice_SetRenderTarget",{surface,depth_surface});
                write_packet({{0x0210,0x116000}});
                bool stale_offset=false;
                try{bridge.ValidateCapturedTargets();}catch(const std::logic_error&){stale_offset=true;}
                if(!stale_offset)throw std::runtime_error("Stale raw surface address accepted");
                write_packet({{0x0210,0x110000},{0x020c,0x01000200}});
                bool wrong_pitch=false;
                try{bridge.ValidateCapturedTargets();}catch(const std::logic_error&){wrong_pitch=true;}
                if(!wrong_pitch)throw std::runtime_error("Wrong raw surface pitch accepted");
                write_packet({{0x020c,0x01000100}});
                bridge.ValidateCapturedTargets();
                bool bad_surface_format=false;
                try{write_packet({{0x0208,0x1128}});}catch(const std::logic_error&){bad_surface_format=true;}
                if(!bad_surface_format)throw std::runtime_error("Multisampled surface silently accepted");
                backend.ReadbackColor(bridge.Snapshot().rt);
                Expect("main target restored",backend.ReadPixel(32,32),{255,192,192,255});
                if(bridge_mem.Read32(0x80116000)!=0xff102030)
                    throw std::runtime_error("Secondary target not stored on switch");
                call("D3DDevice_SetRenderTarget",{copy_surface,depth_surface});
                uint32_t small_rect[4]={30,30,34,34};bridge_mem.Write(rect,small_rect,sizeof(small_rect));
                call("D3DDevice_Clear",{1,rect,0xf0,0xff00ff00,std::bit_cast<uint32_t>(1.f),0});
                Expect("secondary restored for partial clear",backend.ReadPixel(32,32),{0,255,0,255});
                Expect("secondary outside retained",backend.ReadPixel(0,0),{16,32,48,255});
                // Reject a partially overlapping color backing before
                // corrupting the previously valid active target.
                constexpr uint32_t alias_surface=0x20700;
                bridge_mem.Write32(alias_surface,0x50000);bridge_mem.Write32(alias_surface+4,0x116100);
                bridge_mem.Write32(alias_surface+12,0x00011221);bridge_mem.Write32(alias_surface+16,63|(63<<12)|(3<<24));
                call("D3DDevice_SetRenderTarget",{alias_surface,depth_surface});
                bool rejected_alias=false;
                try{backend.ReadbackColor(bridge.Snapshot().rt);}catch(const std::logic_error&){rejected_alias=true;}
                if(!rejected_alias)throw std::runtime_error("Partial color target alias accepted");
                Expect("active target unchanged after rejected alias",backend.ReadPixel(32,32),{0,255,0,255});
                // Separate UV sets in the original fixed vertex declaration.
                // Both stage bindings are real guest resources, not fixtures.
                call("D3DDevice_SetRenderTarget",{surface,depth_surface});
                struct TwoUV{float x,y,z;uint32_t color;float u0,v0,u1,v1;};
                const TwoUV mesh[]={{-.75f,-.75f,.5f,0xffffffff,.25f,.25f,.75f,.25f},
                    {.75f,-.75f,.5f,0xffffffff,.25f,.25f,.75f,.25f},
                    {0,.75f,.5f,0xffffffff,.25f,.25f,.75f,.25f}};
                gpu::VertexShader multi_layout;gpu::FvfInputs(0x242,multi_layout);
                for(unsigned k=0;k<16;++k){
                    auto at=decl+0x14+16*k;auto in=multi_layout.inputs[k];
                    for(unsigned j=0;j<4;++j)bridge_mem.Write32(at+4*j,0);
                    if(in.stream>=0){
                        bridge_mem.Write32(at,uint32_t(in.stream));bridge_mem.Write32(at+4,in.offset);bridge_mem.Write32(at+8,in.type);
                    }
                }
                bridge_mem.Write(0x80114000,mesh,sizeof(mesh));
                call("D3DDevice_SetStreamSource",{0,buffer,sizeof(TwoUV)});
                uint32_t different_uv[4]={0xffff0000,0xff00ff00,0xff0000ff,0xffffff00};
                bridge_mem.Write(0x80115100,different_uv,sizeof(different_uv));
                auto multi_state=bridge.Snapshot();
                multi_state.tss[1][kTssTexCoordIndex]=1;
                multi_state.tss[1][kTssColorOp]=4;
                bridge.AdoptLiveSetters(multi_state);
                gpu::Assembled prepared;
                if(bridge.SyncFixedDiffuseShader(device,true)!=0x242||
                   bridge.VerifyGuestGeometry(5,0,3,0,&prepared)!=3)
                    throw std::runtime_error("Multi UV declaration capture failed");
                call("D3DDevice_Clear",{0,0,0xf0,0xff000000,std::bit_cast<uint32_t>(1.f),0});
                bridge.ReplayCapturedShaderDiagnostic(5,0,3,0,true,&prepared);
                Expect("independent second UV set",backend.ReadPixel(32,32),{0,128,0,255});
                multi_state=bridge.Snapshot();multi_state.tss[1][kTssTexCoordIndex]=0;
                bridge.AdoptLiveSetters(multi_state);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3,0,true);
                Expect("select first UV set",backend.ReadPixel(32,32),{255,0,0,255});
                multi_state=bridge.Snapshot();multi_state.tss[1][kTssTexCoordIndex]=2;
                bridge.AdoptLiveSetters(multi_state);
                bool missing_uv=false;
                try{bridge.ReplayCapturedShaderDiagnostic(5,0,3,0,true);}catch(const std::logic_error&){missing_uv=true;}
                if(!missing_uv)throw std::runtime_error("Undeclared UV set accepted");
                // Reuse the live dual-UV draw with a P8 second stage.
                // Palette changes must reach the actual sampled Vulkan image.
                constexpr uint32_t palette_header=0x20800,palette_phys=0x115400;
                bridge_mem.Write32(palette_header,0x30001);
                bridge_mem.Write32(palette_header+4,palette_phys);
                bridge_mem.Write32(palette_header+8,0);
                bridge_mem.Write32(device+0xf9c,palette_header);
                bridge_mem.Write32(tex1+12,0x01110b29);
                const uint8_t p8_indices[]={1,1,1,1};
                bridge_mem.Write(0x80115100,p8_indices,sizeof(p8_indices));
                std::array<uint32_t,256> live_palette{};live_palette[1]=0xff00ff00;
                bridge_mem.Write(0x80000000u|palette_phys,live_palette.data(),sizeof(live_palette));
                write_packet({{0x1b44,0x01110b29},{0x1b60,palette_phys}});
                multi_state=bridge.Snapshot();multi_state.tss[1][kTssTexCoordIndex]=1;
                bridge.AdoptLiveSetters(multi_state);
                bridge.SyncFixedDiffuseShader(device,true);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3,0,true);
                Expect("live P8 palette sample",backend.ReadPixel(32,32),{0,128,0,255});
                live_palette[1]=0xff0000ff;
                bridge_mem.Write(0x80000000u|palette_phys,live_palette.data(),sizeof(live_palette));
                bridge.SyncFixedDiffuseShader(device,true);
                bridge.ReplayCapturedShaderDiagnostic(5,0,3,0,true);
                Expect("live P8 palette mutation",backend.ReadPixel(32,32),{0,0,64,255});
                write_packet({{0x1b60,palette_phys|4}});
                bool short_palette=false;
                try{bridge.SyncFixedDiffuseShader(device,true);}catch(const std::logic_error&){short_palette=true;}
                if(!short_palette)throw std::runtime_error("Unimplemented short palette accepted");
                write_packet({{0x1b60,palette_phys+64}});
                bool wrong_palette=false;
                try{bridge.SyncFixedDiffuseShader(device,true);}catch(const std::logic_error&){wrong_palette=true;}
                if(!wrong_palette)throw std::runtime_error("Mismatched raw palette accepted");
                bridge_mem.Write32(device+0xf9c,0);
                bridge_mem.Write32(tex1+12,0x01110629);
                bridge_mem.Write(0x80115100,different_uv,sizeof(different_uv));
                write_packet({{0x1b44,0x01110629}});
                // Check every admitted count and position type with live
                // textures bound, including even counts that lack bit 8.
                for(uint32_t uv_count=1;uv_count<=4;++uv_count){
                    for(uint32_t pos:{2u,4u}){
                        const uint32_t candidate=0x40u|pos|(uv_count<<8);
                        gpu::VertexShader layout;gpu::FvfInputs(candidate,layout);
                        for(unsigned k=0;k<16;++k){
                            auto at=decl+0x14+16*k;const auto in=layout.inputs[k];
                            for(unsigned j=0;j<4;++j)bridge_mem.Write32(at+4*j,0);
                            if(in.stream>=0){
                                bridge_mem.Write32(at,uint32_t(in.stream));
                                bridge_mem.Write32(at+4,in.offset);
                                bridge_mem.Write32(at+8,in.type);
                            }
                        }
                        std::vector<uint8_t> verts(3*layout.stream_stride[0],0);
                        bridge_mem.Write(0x80114000,verts.data(),verts.size());
                        call("D3DDevice_SetStreamSource",{0,buffer,layout.stream_stride[0]});
                        if(bridge.SyncFixedDiffuseShader(device,true)!=candidate||
                           bridge.VerifyGuestGeometry(5,0,3,0)!=3)
                            throw std::runtime_error("Textured FVF count regression");
                        if(uv_count>1){
                            bool legacy=false;
                            try{bridge.SyncFixedDiffuseShader(device,false);}catch(const std::logic_error&){legacy=true;}
                            if(!legacy)throw std::runtime_error("Multi-UV capture enabled on legacy path");
                        }
                    }
                }
            }
            std::puts("PASS D3D8 bridge Clear/Draw/Present -> Vulkan -> Xbox memory and rotation");
        }
        std::puts("PASS Vulkan draws, Xbox depth/stencil, checked guest vertex/index assembly, BGRA and DXT1 staging, guest surface readback");
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
