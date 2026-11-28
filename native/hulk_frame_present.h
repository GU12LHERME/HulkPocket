#pragma once
// Per-present CPU work shared by the live swap and the Android window path.
// Both run once per guest frame over the full visible surface, so they use
// word-wide operations the compiler can vectorize (NEON on the RP6) instead
// of per-byte loops. Pitch padding is never read or hashed.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hulkpocket {

// FNV-1a over 64-bit little-endian lanes of each visible row (row tail
// bytes are folded individually). Used only to detect repeated frames
// within one run; values are not comparable with older builds.
inline uint64_t HashVisibleBgra(const uint8_t* pixels, unsigned width, unsigned height, size_t pitch) {
    uint64_t hash = 14695981039346656037ull;
    const size_t row_bytes = size_t(width) * 4;
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t* row = pixels + size_t(y) * pitch;
        size_t x = 0;
        for (; x + 8 <= row_bytes; x += 8) {
            uint64_t lane;
            std::memcpy(&lane, row + x, sizeof lane);
            hash ^= lane;
            hash *= 1099511628211ull;
        }
        for (; x < row_bytes; ++x) {
            hash ^= row[x];
            hash *= 1099511628211ull;
        }
        // Separate rows so identical bytes shifted across a row boundary differ.
        hash ^= y;
        hash *= 1099511628211ull;
    }
    return hash;
}

// Xbox BGRA (A ignored) -> Android RGBA_8888 with opaque alpha.
inline void SwizzleBgraToOpaqueRgba(const uint8_t* src, uint8_t* dst, unsigned width) {
    for (unsigned x = 0; x < width; ++x) {
        uint32_t p;
        std::memcpy(&p, src + size_t(x) * 4, sizeof p);
        const uint32_t out = (p & 0x0000FF00u) | ((p >> 16) & 0x000000FFu) | ((p & 0x000000FFu) << 16) | 0xFF000000u;
        std::memcpy(dst + size_t(x) * 4, &out, sizeof out);
    }
}

}  // namespace hulkpocket
