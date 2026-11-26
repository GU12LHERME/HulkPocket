#include "gpu/texture.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "gpu/guest_memory.h"
#include <stdexcept>
#include <limits>

namespace gpu {

namespace {

const FormatInfo kUnknown{"unknown", 32, false, false, false, Host::None};

struct Entry {
    uint32_t id;
    FormatInfo info;
};

// console texture formats (XDK D3DFORMAT values) and how they reach the host
const Entry kFormats[] = {
    {0x00, {"L8", 8, true, false, false, Host::BGRA8}},
    {0x01, {"AL8", 8, true, false, false, Host::BGRA8}},
    {0x02, {"A1R5G5B5", 16, true, false, false, Host::BGRA8}},
    {0x03, {"X1R5G5B5", 16, true, false, false, Host::BGRA8}},
    {0x04, {"A4R4G4B4", 16, true, false, false, Host::BGRA8}},
    {0x05, {"R5G6B5", 16, true, false, false, Host::BGRA8}},
    {0x06, {"A8R8G8B8", 32, true, false, false, Host::BGRA8}},
    {0x07, {"X8R8G8B8", 32, true, false, false, Host::BGRA8}},
    {0x0B, {"P8", 8, true, false, false, Host::BGRA8}},
    {0x0C, {"DXT1", 8, false, true, false, Host::BC1}},
    {0x0E, {"DXT3", 16, false, true, false, Host::BC2}},
    {0x0F, {"DXT5", 16, false, true, false, Host::BC3}},
    {0x10, {"LIN_A1R5G5B5", 16, false, false, false, Host::BGRA8}},
    {0x11, {"LIN_R5G6B5", 16, false, false, false, Host::BGRA8}},
    {0x12, {"LIN_A8R8G8B8", 32, false, false, false, Host::BGRA8}},
    {0x13, {"LIN_L8", 8, false, false, false, Host::BGRA8}},
    {0x16, {"LIN_R8B8", 16, false, false, false, Host::BGRA8}},
    {0x17, {"LIN_G8B8", 16, false, false, false, Host::BGRA8}},
    {0x19, {"A8", 8, true, false, false, Host::BGRA8}},
    {0x1A, {"A8L8", 16, true, false, false, Host::BGRA8}},
    {0x1B, {"LIN_AL8", 8, false, false, false, Host::BGRA8}},
    {0x1C, {"LIN_X1R5G5B5", 16, false, false, false, Host::BGRA8}},
    {0x1D, {"LIN_A4R4G4B4", 16, false, false, false, Host::BGRA8}},
    {0x1E, {"LIN_X8R8G8B8", 32, false, false, false, Host::BGRA8}},
    {0x1F, {"LIN_A8", 8, false, false, false, Host::BGRA8}},
    {0x20, {"LIN_A8L8", 16, false, false, false, Host::BGRA8}},
    {0x24, {"YUY2", 16, false, false, false, Host::BGRA8}},
    {0x25, {"UYVY", 16, false, false, false, Host::BGRA8}},
    {0x27, {"L6V5U5", 16, true, false, false, Host::BGRA8}},
    {0x28, {"V8U8", 16, true, false, false, Host::BGRA8}},
    {0x29, {"R8B8", 16, true, false, false, Host::BGRA8}},
    {0x2A, {"D24S8", 32, true, false, true, Host::R32F}},
    {0x2B, {"F24S8", 32, true, false, true, Host::R32F}},
    {0x2C, {"D16", 16, true, false, true, Host::R32F}},
    {0x2D, {"F16", 16, true, false, true, Host::R32F}},
    {0x2E, {"LIN_D24S8", 32, false, false, true, Host::R32F}},
    {0x2F, {"LIN_F24S8", 32, false, false, true, Host::R32F}},
    {0x30, {"LIN_D16", 16, false, false, true, Host::R32F}},
    {0x31, {"LIN_F16", 16, false, false, true, Host::R32F}},
    {0x32, {"L16", 16, true, false, false, Host::BGRA8}},
    {0x33, {"V16U16", 32, true, false, false, Host::BGRA8}},
    {0x35, {"LIN_L16", 16, false, false, false, Host::BGRA8}},
    {0x36, {"LIN_V16U16", 32, false, false, false, Host::BGRA8}},
    {0x37, {"LIN_L6V5U5", 16, false, false, false, Host::BGRA8}},
    {0x38, {"R5G5B5A1", 16, true, false, false, Host::BGRA8}},
    {0x39, {"R4G4B4A4", 16, true, false, false, Host::BGRA8}},
    {0x3A, {"A8B8G8R8", 32, true, false, false, Host::BGRA8}},
    {0x3B, {"B8G8R8A8", 32, true, false, false, Host::BGRA8}},
    {0x3C, {"R8G8B8A8", 32, true, false, false, Host::BGRA8}},
    {0x3D, {"LIN_R5G5B5A1", 16, false, false, false, Host::BGRA8}},
    {0x3E, {"LIN_R4G4B4A4", 16, false, false, false, Host::BGRA8}},
    {0x3F, {"LIN_A8B8G8R8", 32, false, false, false, Host::BGRA8}},
    {0x40, {"LIN_B8G8R8A8", 32, false, false, false, Host::BGRA8}},
    {0x41, {"LIN_R8G8B8A8", 32, false, false, false, Host::BGRA8}},
};

uint32_t Log2(uint32_t v) {
    uint32_t n = 0;
    while ((1u << n) < v) n++;
    return n;
}

// Morton order: bits of x, y and z interleave from the lowest, an axis drops out once its size is used up
void SwizzleMasks(uint32_t w, uint32_t h, uint32_t d, uint32_t& mx, uint32_t& my, uint32_t& mz) {
    mx = my = mz = 0;
    uint32_t bit = 1, lx = Log2(w), ly = Log2(h), lz = Log2(d);
    for (uint32_t i = 0; bit && (i < lx || i < ly || i < lz); i++) {
        if (i < lx) {
            mx |= bit;
            bit <<= 1;
        }
        if (i < ly) {
            my |= bit;
            bit <<= 1;
        }
        if (i < lz) {
            mz |= bit;
            bit <<= 1;
        }
    }
}

uint32_t Deposit(uint32_t v, uint32_t mask) {
    uint32_t r = 0;
    for (uint32_t bit = 1; mask; bit <<= 1) {
        uint32_t low = mask & (~mask + 1);
        if (v & bit) r |= low;
        mask &= mask - 1;
    }
    return r;
}

uint8_t Expand5(uint32_t v) { return uint8_t((v << 3) | (v >> 2)); }
uint8_t Expand6(uint32_t v) { return uint8_t((v << 2) | (v >> 4)); }
uint8_t Expand4(uint32_t v) { return uint8_t(v * 17); }
uint8_t Clamp255(int v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }

// one texel to B, G, R, A
void Texel(uint32_t fmt, const uint8_t* p, uint8_t* o) {
    uint16_t s16;
    s16 = Format(fmt).bits >= 16 ? uint16_t(p[0] | (uint16_t(p[1]) << 8)) : 0;
    switch (fmt) {
        case 0x00: case 0x13:
            o[0] = o[1] = o[2] = p[0];
            o[3] = 255;
            break;
        case 0x01: case 0x1B:
            o[0] = o[1] = o[2] = o[3] = p[0];
            break;
        case 0x19: case 0x1F:
            o[0] = o[1] = o[2] = 255;
            o[3] = p[0];
            break;
        case 0x1A: case 0x20:
            o[0] = o[1] = o[2] = p[0];
            o[3] = p[1];
            break;
        case 0x02: case 0x10:
            o[0] = Expand5(s16 & 31);
            o[1] = Expand5((s16 >> 5) & 31);
            o[2] = Expand5((s16 >> 10) & 31);
            o[3] = (s16 & 0x8000) ? 255 : 0;
            break;
        case 0x03: case 0x1C:
            o[0] = Expand5(s16 & 31);
            o[1] = Expand5((s16 >> 5) & 31);
            o[2] = Expand5((s16 >> 10) & 31);
            o[3] = 255;
            break;
        case 0x04: case 0x1D:
            o[0] = Expand4(s16 & 15);
            o[1] = Expand4((s16 >> 4) & 15);
            o[2] = Expand4((s16 >> 8) & 15);
            o[3] = Expand4(s16 >> 12);
            break;
        case 0x05: case 0x11:
            o[0] = Expand5(s16 & 31);
            o[1] = Expand6((s16 >> 5) & 63);
            o[2] = Expand5(s16 >> 11);
            o[3] = 255;
            break;
        case 0x06: case 0x12:
            std::memcpy(o, p, 4);
            break;
        case 0x07: case 0x1E:
            std::memcpy(o, p, 3);
            o[3] = 255;
            break;
        case 0x3A: case 0x3F:  // memory R G B A
            o[0] = p[2];
            o[1] = p[1];
            o[2] = p[0];
            o[3] = p[3];
            break;
        case 0x3B: case 0x40:  // memory A R G B
            o[0] = p[3];
            o[1] = p[2];
            o[2] = p[1];
            o[3] = p[0];
            break;
        case 0x3C: case 0x41:  // memory A B G R
            o[0] = p[1];
            o[1] = p[2];
            o[2] = p[3];
            o[3] = p[0];
            break;
        case 0x38: case 0x3D:
            o[3] = (s16 & 1) ? 255 : 0;
            o[0] = Expand5((s16 >> 1) & 31);
            o[1] = Expand5((s16 >> 6) & 31);
            o[2] = Expand5(s16 >> 11);
            break;
        case 0x39: case 0x3E:
            o[3] = Expand4(s16 & 15);
            o[0] = Expand4((s16 >> 4) & 15);
            o[1] = Expand4((s16 >> 8) & 15);
            o[2] = Expand4(s16 >> 12);
            break;
        case 0x28: case 0x17:  // two channels, low byte then high byte
            o[2] = p[0];
            o[1] = p[1];
            o[0] = p[0];
            o[3] = p[1];
            break;
        case 0x29: case 0x16:
            o[2] = p[1];
            o[1] = p[0];
            o[0] = p[0];
            o[3] = p[1];
            break;
        case 0x27: case 0x37:
            o[2] = Expand5(s16 & 31);
            o[1] = Expand5((s16 >> 5) & 31);
            o[0] = Expand6(s16 >> 10);
            o[3] = 255;
            break;
        case 0x32: case 0x35:
            o[0] = o[1] = o[2] = p[1];
            o[3] = 255;
            break;
        case 0x33: case 0x36:
            o[2] = p[1];
            o[1] = p[3];
            o[0] = 0;
            o[3] = 255;
            break;
        default:
            o[0] = o[1] = o[2] = p[0];
            o[3] = 255;
            break;
    }
}

float DepthTexel(uint32_t fmt, const uint8_t* p) {
    uint32_t v=0;
    uint16_t s=0;
    if (Format(fmt).bits == 32) std::memcpy(&v, p, 4);
    std::memcpy(&s, p, 2);
    switch (fmt) {
        case 0x2A: case 0x2E: return float(v >> 8) / 16777215.0f;
        case 0x2C: case 0x30: return float(s) / 65535.0f;
        case 0x2B: case 0x2F: {  // unsigned, 8 bit exponent, 16 bit mantissa: a float32 missing its low bits
            uint32_t exponent = v >> 24, mantissa = (v >> 8) & 0xFFFF;
            return std::bit_cast<float>(exponent << 23 | mantissa << 7);
        }
        default: {  // unsigned, 4 bit exponent biased 7, 12 bit mantissa, over its max 511.9375
            uint32_t exponent = (s >> 12) + 120u, mantissa = s & 0xFFFu;
            return std::bit_cast<float>(exponent << 23 | mantissa << 11) / 511.9375f;
        }
    }
}

std::mutex g_dirty_lock;
std::unordered_map<uint32_t, uint64_t> g_dirty;
uint64_t g_epoch = 1;

}  // namespace

// by format byte: asked many times a draw
const FormatInfo& Format(uint32_t f) {
    static const auto table = [] {
        std::array<const FormatInfo*, 256> t;
        t.fill(&kUnknown);
        for (auto& e : kFormats) t[e.id] = &e.info;
        return t;
    }();
    return f < table.size() ? *table[f] : kUnknown;
}

bool Describe(uint32_t header, TextureDesc& d) {
    if (!header) return false;
    CheckedGuestRange(header,20);
    uint32_t common = ReadGuest32(header);
    uint32_t type = common & 0x70000;  // 0x40000 texture, 0x50000 surface
    if (type != 0x40000 && type != 0x50000) return false;
    // D3DPixelContainer: Common, Data, Lock, Format, Size
    uint32_t fmt = ReadGuest32(header + 12), size = ReadGuest32(header + 16);
    d.data = PhysicalResourceAddress(ReadGuest32(header + 4));
    // Format: cube bit 2, dimensions bits 4 to 7, format 8 to 15, levels 16 to 19, log2 sizes from bit 20
    d.format = (fmt >> 8) & 0xFF;
    d.cube = fmt & 4;
    uint32_t dims = (fmt >> 4) & 0xF;
    d.levels = std::max<uint32_t>((fmt >> 16) & 0xF, 1);
    if (size) {
        d.width = (size & 0xFFF) + 1;
        d.height = ((size >> 12) & 0xFFF) + 1;
        d.pitch = ((size >> 24) + 1) * 64;
        d.depth = 1;
        d.levels = 1;
        d.linear = true;
    } else {
        d.width = 1u << ((fmt >> 20) & 0xF);
        d.height = 1u << ((fmt >> 24) & 0xF);
        d.depth = dims == 3 ? 1u << ((fmt >> 28) & 0xF) : 1;
        d.linear = !Format(d.format).swizzled && !Format(d.format).compressed;
        d.pitch = d.width * Format(d.format).bits / 8;
    }
    return true;
}

bool DescribeSurface(const Surface& s, TextureDesc& d) {
    if (!s.header) return false;
    return Describe(s.header, d);
}

uint32_t LevelSize(const TextureDesc& d, uint32_t level) {
    const FormatInfo& f = Format(d.format);
    if (d.linear) return d.pitch * d.height;
    uint32_t w = std::max(d.width >> level, 1u), h = std::max(d.height >> level, 1u), z = std::max(d.depth >> level, 1u);
    if (f.compressed) return std::max(1u, (w + 3) / 4) * std::max(1u, (h + 3) / 4) * f.bits * z;
    return w * h * z * f.bits / 8;
}

uint32_t LevelOffset(const TextureDesc& d, uint32_t level) {
    uint32_t off = 0;
    for (uint32_t l = 0; l < level; l++) off += LevelSize(d, l);
    return off;
}

uint32_t FaceSize(const TextureDesc& d) { return (LevelOffset(d, d.levels) + 127) & ~127u; }

uint32_t TotalSize(const TextureDesc& d) { return d.cube ? FaceSize(d) * 6 : LevelOffset(d, d.levels); }

static void DecodeLevelUnchecked(const TextureDesc& d, uint32_t level, uint32_t face, const uint8_t* guest, const uint32_t* palette,
                 std::vector<uint8_t>& out, uint32_t& row_pitch) {
    const FormatInfo& f = Format(d.format);
    uint32_t w = d.linear ? d.width : std::max(d.width >> level, 1u);
    uint32_t h = d.linear ? d.height : std::max(d.height >> level, 1u);
    uint32_t z = d.linear ? 1 : std::max(d.depth >> level, 1u);
    const uint8_t* src = guest + (d.cube ? face * FaceSize(d) : 0) + LevelOffset(d, level);
    if (f.compressed) {
        uint32_t bw = std::max(1u, (w + 3) / 4), bh = std::max(1u, (h + 3) / 4);
        row_pitch = bw * f.bits;
        out.assign(src, src + size_t(row_pitch) * bh * z);
        return;
    }
    if (f.host == Host::R32F) {
        row_pitch = w * 4;
        out.resize(size_t(row_pitch) * h * z);
        float* o = reinterpret_cast<float*>(out.data());
        uint32_t bpp = f.bits / 8;
        uint32_t mx, my, mz;
        SwizzleMasks(w, h, z, mx, my, mz);
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                uint32_t off = d.linear ? y * d.pitch + x * bpp : (Deposit(x, mx) | Deposit(y, my)) * bpp;
                o[y * w + x] = DepthTexel(d.format, src + off);
            }
        return;
    }
    row_pitch = w * 4;
    out.resize(size_t(row_pitch) * h * z);
    uint32_t bpp = f.bits / 8;
    if (d.format == 0x24 || d.format == 0x25) {
        // packed 4:2:2, two pixels share chroma
        bool yuy2 = d.format == 0x24;
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x + 1 < w + 1; x += 2) {
                const uint8_t* p = src + y * d.pitch + x * 2;
                int y0 = yuy2 ? p[0] : p[1], u = yuy2 ? p[1] : p[0], y1 = yuy2 ? p[2] : p[3], v = yuy2 ? p[3] : p[2];
                for (int k = 0; k < 2 && x + uint32_t(k) < w; k++) {
                    int c = (k ? y1 : y0) - 16, dd = u - 128, e = v - 128;  // BT.601 studio range
                    uint8_t* o = &out[(y * w + x + uint32_t(k)) * 4];
                    o[2] = Clamp255((298 * c + 409 * e + 128) >> 8);
                    o[1] = Clamp255((298 * c - 100 * dd - 208 * e + 128) >> 8);
                    o[0] = Clamp255((298 * c + 516 * dd + 128) >> 8);
                    o[3] = 255;
                }
            }
        return;
    }
    if (d.linear) {
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) Texel(d.format, src + y * d.pitch + x * bpp, &out[(y * w + x) * 4]);
        return;
    }
    uint32_t mx, my, mz;
    SwizzleMasks(w, h, z, mx, my, mz);
    std::vector<uint32_t> xo(w), yo(h), zo(z);
    for (uint32_t x = 0; x < w; x++) xo[x] = Deposit(x, mx);
    for (uint32_t y = 0; y < h; y++) yo[y] = Deposit(y, my);
    for (uint32_t k = 0; k < z; k++) zo[k] = Deposit(k, mz);
    for (uint32_t k = 0; k < z; k++)
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                const uint8_t* p = src + size_t(xo[x] | yo[y] | zo[k]) * bpp;
                uint8_t* o = &out[((size_t(k) * h + y) * w + x) * 4];
                if (d.format == 0x0B && palette) {
                    uint32_t c = palette[p[0]];
                    std::memcpy(o, &c, 4);
                } else {
                    Texel(d.format, p, o);
                }
            }
}

// Validate dimensions and total arithmetic before using the upstream decoder.
void DecodeGuestLevel(const TextureDesc& d,uint32_t level,uint32_t face,
                      uint32_t palette_address,std::vector<uint8_t>& out,uint32_t& row_pitch) {
    const auto& f=Format(d.format);
    constexpr uint64_t max_bytes=64ull*1024*1024;
    auto pow2=[](uint32_t n){return n && !(n&(n-1));};
    if(f.host==Host::None || !d.width || !d.height || !d.depth ||
       d.width>4096 || d.height>4096 || d.depth>256 ||
       !d.levels || d.levels>13 || level>=d.levels ||
       face>=(d.cube?6u:1u) || (d.cube && (d.width!=d.height || d.depth!=1)) ||
       (d.linear && (d.levels!=1 || d.depth!=1 || d.cube || f.compressed || f.swizzled)) ||
       (!d.linear && (!pow2(d.width)||!pow2(d.height)||!pow2(d.depth))) ||
       (f.depth && d.depth!=1) || (!d.linear && !f.swizzled && !f.compressed))
        throw std::invalid_argument("Unsupported or invalid Xbox texture layout");
    if(d.linear && (d.pitch<uint64_t(d.width)*f.bits/8 ||
       ((d.format==0x24 || d.format==0x25) && (d.width&1))))
        throw std::invalid_argument("Invalid linear texture pitch");
    uint64_t chain=0;
    for(uint32_t l=0;l<d.levels;++l) {
        uint64_t w=std::max(d.width>>l,1u),h=std::max(d.height>>l,1u),z=std::max(d.depth>>l,1u);
        uint64_t bytes=d.linear?uint64_t(d.pitch)*d.height:
            f.compressed?((w+3)/4)*((h+3)/4)*z*f.bits:w*h*z*f.bits/8;
        if(bytes>max_bytes || chain+bytes>max_bytes || (!f.compressed && w*h*z*4>max_bytes))
            throw std::invalid_argument("Xbox texture exceeds diagnostic size cap");
        chain+=bytes;
    }
    uint64_t total=d.cube?((chain+127)&~uint64_t(127))*6:chain;
    if(total>max_bytes) throw std::invalid_argument("Xbox texture exceeds diagnostic size cap");
    const auto* input=CheckedGuestRange(d.data,total);
    std::array<uint32_t,256> palette{};
    const uint32_t* colors=nullptr;
    if(d.format==0x0b) {
        if(!palette_address)throw std::invalid_argument("Missing Xbox texture palette");
        std::memcpy(palette.data(),CheckedGuestRange(palette_address,sizeof(palette)),sizeof(palette));
        colors=palette.data();
    }
    DecodeLevelUnchecked(d,level,face,input,colors,out,row_pitch);
}

uint32_t LinearColorFormat(uint32_t f) {
    switch (f) {
        case 0x06: return 0x12;
        case 0x07: return 0x1E;
        case 0x05: return 0x11;
        case 0x02: return 0x10;
        case 0x03: return 0x1C;
        default: return Format(f).swizzled ? 0x1E : (f ? f : 0x1E);
    }
}

uint32_t LinearDepthFormat(uint32_t f) {
    switch (f) {
        case 0x2A: return 0x2E;
        case 0x2B: return 0x2F;
        case 0x2C: return 0x30;
        case 0x2D: return 0x31;
        default: return Format(f).depth ? f : 0x2E;
    }
}

void SurfaceSize(const Surface& s, uint32_t& w, uint32_t& h) {
    TextureDesc d;
    if (Describe(s.header, d)) {
        w = d.width;
        h = d.height;
    } else {
        w = 640;
        h = 480;
    }
}

void MarkDirty(uint32_t res) {
    if (!res) return;
    uint32_t data = PhysicalResourceAddress(ReadGuest32(uint64_t(res) + 4));
    std::lock_guard<std::mutex> lock(g_dirty_lock);
    g_dirty[data] = ++g_epoch;
}

uint64_t DirtyEpoch(uint32_t data) {
    std::lock_guard<std::mutex> lock(g_dirty_lock);
    auto it = g_dirty.find(data);
    return it == g_dirty.end() ? 0 : it->second;
}

// Surface copies need overlap, rectangle and GPU synchronization semantics.
bool CopySurfaceMemory(const Surface&, const Surface&, uint32_t, uint32_t, uint32_t) {
    throw std::logic_error("GPU surface copy not integrated");
}

// declarations

static uint32_t TypeSize(uint32_t t) {
    uint32_t kind = t & 15, count = (t >> 4) & 15;
    switch (kind) {
        case 0: return 4;                // D3DCOLOR
        case 1: case 5: return 2 * count;  // shorts
        case 2: return 4 * count;        // floats
        case 4: return count;            // bytes
        case 6: return 4;                // packed 11:11:10
        default: return 4 * count;
    }
}

void ParseDeclaration(VertexShader& vs) {
    int stream = 0;
    uint32_t offset = 0;
    vs.stream_stride.fill(0);
    vs.inputs.fill({});
    vs.constants.clear();
    for (size_t i = 0; i < vs.decl.size(); i++) {
        uint32_t t = vs.decl[i];
        if (t == 0xFFFFFFFF) break;  // D3DVSD_END
        uint32_t kind = t >> 29;
        if (kind == 1) {  // STREAM
            if (t & (1u << 28)) continue;  // tessellator stream
            vs.stream_stride[size_t(stream)] = std::max(vs.stream_stride[size_t(stream)], offset);
            stream = int(t & 15);
            offset = vs.stream_stride[size_t(stream)];
        } else if (kind == 2) {  // STREAMDATA
            if (t & (1u << 28)) {
                // SKIP counts dwords, SKIPBYTES bytes
                uint32_t n = (t >> 16) & 0xFF;
                offset += (t & (1u << 27)) ? n : n * 4;
            } else {
                uint32_t reg = t & 15, type = (t >> 16) & 0xFF;
                if (type != 0x02) vs.inputs[reg] = {stream, offset, type};  // 0x02 D3DVSDT_NONE
                offset += type == 0x02 ? 0 : TypeSize(type);
            }
        } else if (kind == 4) {  // CONSTMEM
            uint32_t count = (t >> 25) & 15, reg = t & 0xFF;
            for (uint32_t c = 0; c < count && i + 4 < vs.decl.size(); c++) {
                std::array<float, 4> v;
                std::memcpy(v.data(), &vs.decl[i + 1], 16);
                vs.constants.push_back({reg + c, v});
                i += 4;
            }
        }
    }
    vs.stream_stride[size_t(stream)] = std::max(vs.stream_stride[size_t(stream)], offset);
}

// pixel shader constants

uint32_t PackColor(const std::array<float, 4>& c) {
    auto q = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return q(c[3]) << 24 | q(c[0]) << 16 | q(c[1]) << 8 | q(c[2]);
}

// PSC0Mapping and PSC1Mapping give each combiner stage the D3D constant its C0 and C1 read,
// PSFinalCombinerConstants does the same for the final combiner. 0xF leaves the literal in place.
void ApplyPixelConstants(State& s, uint32_t first, uint32_t count) {
    if (!s.ps) return;
    const uint32_t* d = s.ps->def;
    uint32_t c0map = d[57], c1map = d[58], fmap = d[59];
    auto mine = [&](uint32_t m) { return m >= first && m < first + count && m < 8; };
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t m0 = (c0map >> (i * 4)) & 0xF, m1 = (c1map >> (i * 4)) & 0xF;
        if (mine(m0)) s.simple[(kMPsConstant0 >> 2) + i] = PackColor(s.psc[m0]);
        if (mine(m1)) s.simple[(kMPsConstant1 >> 2) + i] = PackColor(s.psc[m1]);
    }
    uint32_t f0 = fmap & 0xF, f1 = (fmap >> 4) & 0xF;
    if (mine(f0)) s.simple[kMPsFinalConstant0 >> 2] = PackColor(s.psc[f0]);
    if (mine(f1)) s.simple[kMPsFinalConstant1 >> 2] = PackColor(s.psc[f1]);
}

}  // namespace gpu
