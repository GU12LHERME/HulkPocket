#pragma once
#include "xbe_d3d8_device_memory.h"
#include "xbe_nv2a_push.h"
#include <cstdio>
namespace hulkpocket {
// Diagnostic state capture, not a renderer. Unsupported rendering states
// may be recorded here but must never be treated as rendered successfully.
class XbeD3dDefaultTrace {
    XboxGuestAddressSpace& mem_;
    gpu::State state_{};
    unsigned calls_=0;
    void Log(const char* kind,uint32_t a,uint32_t b,uint32_t value){
        if(calls_++>=2048)throw std::runtime_error("D3D default setter budget exceeded");
        std::fprintf(stderr,"D3D DEFAULT %s a=%08X b=%08X value=%08X\n",kind,a,b,value);
    }
    void Mirror(uint32_t base,uint32_t index,uint32_t value){
        uint64_t a=uint64_t(base)+4ull*index;
        if(!base||a>UINT32_MAX||!mem_.IsCommitted(uint32_t(a),4))
            throw std::out_of_range("D3D default table address");
        mem_.Write32(uint32_t(a),value);
    }
public:
    explicit XbeD3dDefaultTrace(XboxGuestAddressSpace& mem):mem_(mem){}
    void Simple(uint32_t method,uint32_t value){
        method&=0x1ffc;Log("simple",method,0,value);state_.simple[method>>2]=value;
    }
    void Render(uint32_t encode,uint32_t table,uint32_t index,uint32_t value){
        if(index>=gpu::kRsMax)throw std::out_of_range("D3D render state index");
        if(index<gpu::kRsDeferredFirst){
            uint64_t a=uint64_t(encode)+4ull*index;
            if(!encode||a>UINT32_MAX||!mem_.IsCommitted(uint32_t(a),4))
                throw std::out_of_range("D3D render encode table");
            Simple(mem_.Read32(uint32_t(a)),value);
        }
        Log("render",index,0,value);Mirror(table,index,value);state_.rs[index]=value;
    }
    void Texture(uint32_t table,uint32_t stage,uint32_t index,uint32_t value){
        if(stage>=4||index>=gpu::kTssCount)throw std::out_of_range("D3D texture state index");
        Log("texture",stage,index,value);Mirror(table,stage*32+index,value);state_.tss[stage][index]=value;
    }
    const gpu::State& State()const{return state_;}
    void Dump(XbeD3d8DeviceMemory& buffers){
        auto begin=buffers.Scratch(),end=buffers.ScratchEnd();
        std::fprintf(stderr,"D3D DEFAULT TRACE: setters=%u scratch_bytes=%u\n",calls_,end-begin);
        try{
            auto methods=DecodeNv2aMethods(mem_,begin,end);
            std::fprintf(stderr,"D3D DEFAULT PACKETS: methods=%zu\n",methods.size());
            unsigned n=0;for(auto m:methods){
                if(n++>=512){std::fprintf(stderr,"D3D DEFAULT PACKETS truncated\n");break;}
                std::fprintf(stderr,"D3D DEFAULT PUSH method=%04X value=%08X\n",m.method,m.value);
            }
        }catch(const std::exception& e){
            std::fprintf(stderr,"D3D DEFAULT PACKETS decode failed: %s\n",e.what());
            for(uint32_t a=begin;a<end&&a-begin<2048;a+=4)
                std::fprintf(stderr,"D3D DEFAULT RAW +%04X=%08X\n",a-begin,mem_.Read32(a));
        }
    }
};
}
