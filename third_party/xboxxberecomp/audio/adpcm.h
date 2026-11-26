#pragma once
// Derived from TGP482/XboxXBERecomp runtime/xdk/dsound.cpp
// commit 767a81837fba2d86913f6af81a10b33661d35518, MIT. See LICENSE in this directory.
#include <algorithm>
#include <cstdint>
#include <array>
#include <cstddef>
#include <stdexcept>
namespace hulkpocket::audio {
namespace detail {
const int kIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int kStep[89] = {7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
                       31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
                       130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
                       544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
                       2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
                       9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

// one 36 byte per channel IMA block, the voice plays 64 of its 65 frames
inline void DecodeBlock(const uint8_t* b, int ch, int16_t* out) {
    for (int c = 0; c < ch; c++) {
        int pred = int16_t(b[c * 4] | b[c * 4 + 1] << 8), idx = std::min<int>(b[c * 4 + 2], 88);
        out[c] = int16_t(pred);
        const uint8_t* d = b + ch * 4;
        for (int k = 0; k < 8; k++)
            for (int j = 0; j < 8; j++) {
                int n = (d[(k * ch + c) * 4 + j / 2] >> ((j & 1) * 4)) & 15;
                int step = kStep[idx], diff = step >> 3;
                if (n & 1) diff += step >> 2;
                if (n & 2) diff += step >> 1;
                if (n & 4) diff += step;
                pred = std::clamp(n & 8 ? pred - diff : pred + diff, -32768, 32767);
                idx = std::clamp(idx + kIndex[n], 0, 88);
                out[(1 + k * 8 + j) * ch + c] = int16_t(pred);
            }
    }
}


}
// An Xbox ADPCM block stores 65 decoded frames, of which the Xbox
// voice consumes 64. Explicit sizes prevent malformed guest buffers
// reaching the upstream decoder with insufficient storage.
inline void DecodeXboxAdpcm(const uint8_t* block,std::size_t block_size,unsigned channels,
                            int16_t* pcm,std::size_t pcm_size){
    if(!block||!pcm||channels<1||channels>8||block_size!=36u*channels||
       pcm_size!=64u*channels)throw std::invalid_argument("Invalid Xbox ADPCM block size");
    int16_t scratch[65*8]{};
    detail::DecodeBlock(block,int(channels),scratch);
    std::copy_n(scratch,pcm_size,pcm);
}
template<std::size_t N,std::size_t M>
inline void DecodeXboxAdpcm(const std::array<uint8_t,N>& block,unsigned channels,
                            std::array<int16_t,M>& pcm){
    DecodeXboxAdpcm(block.data(),N,channels,pcm.data(),M);
}
}
