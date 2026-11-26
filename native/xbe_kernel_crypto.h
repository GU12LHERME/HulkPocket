#pragma once
#include "xbox_guest_address_space.h"
#include "../third_party/xboxxberecomp/kernel/crypto.h"
#include <array>
#include <stdexcept>

namespace hulkpocket {
// Xbox SHA context is 116 bytes. The upstream portable implementation
// stores its own state in that space; callers must use matching routines.
class XbeKernelCrypto {
    XboxGuestAddressSpace& mem_;
    static constexpr size_t kContext=116;
    static constexpr uint32_t kMaxInput=64u*1024u*1024u;
    uint8_t* Buffer(uint32_t address,size_t size) {
        if(!size) return nullptr;
        if(!mem_.IsCommitted(address,size))
            throw std::runtime_error("Invalid crypto guest buffer");
        return mem_.Base()+address;
    }
    const uint8_t* Input(uint32_t address,uint32_t size) {
        if(size>kMaxInput) throw std::runtime_error("Crypto input exceeds diagnostic bound");
        return Buffer(address,size);
    }
public:
    explicit XbeKernelCrypto(XboxGuestAddressSpace& mem):mem_(mem) {}
    void Init(uint32_t address) {
        auto* out=Buffer(address,kContext);
        std::array<uint8_t,kContext> context{};
        crypto::ShaInit(context.data());
        mem_.Write(address,context.data(),context.size());
        (void)out;
    }
    void Update(uint32_t address,uint32_t data,uint32_t size) {
        auto* ctx=Buffer(address,kContext);
        auto* input=Input(data,size);
        crypto::ShaUpdate(ctx,input,size);
    }
    void Final(uint32_t address,uint32_t digest) {
        auto* ctx=Buffer(address,kContext);
        auto* out=Buffer(digest,20);
        std::array<uint8_t,20> result{};
        crypto::ShaFinal(ctx,result.data());
        mem_.Write(digest,result.data(),result.size());
        (void)out;
    }
    void Hmac(uint32_t key,uint32_t key_size,uint32_t first,uint32_t first_size,
              uint32_t second,uint32_t second_size,uint32_t digest) {
        auto* k=Input(key,key_size);
        auto* a=Input(first,first_size);
        auto* b=Input(second,second_size);
        auto* out=Buffer(digest,20);
        static constexpr uint8_t empty=0;
        std::array<uint8_t,20> result{};
        crypto::Hmac(k?k:&empty,key_size,a,first_size,b,second_size,result.data());
        mem_.Write(digest,result.data(),result.size());
        (void)out;
    }
};
}
