// Host/ARM64 checks for the per-present CPU helpers.
#include "hulk_frame_present.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace hulkpocket;

static void Expect(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}

static void ReferenceSwizzle(const uint8_t* src, uint8_t* row, unsigned w) {
    for (unsigned x = 0; x < w; ++x) {
        row[x * 4] = src[x * 4 + 2]; row[x * 4 + 1] = src[x * 4 + 1];
        row[x * 4 + 2] = src[x * 4]; row[x * 4 + 3] = 255;
    }
}

int main() {
    // Odd width exercises the 8-byte lane tail; pitch adds padding bytes.
    const unsigned w = 37, h = 11; const size_t pitch = w * 4 + 12;
    std::vector<uint8_t> frame(pitch * h);
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = uint8_t(i * 131 + 7);

    std::vector<uint8_t> got(w * 4), want(w * 4);
    for (unsigned y = 0; y < h; ++y) {
        SwizzleBgraToOpaqueRgba(frame.data() + y * pitch, got.data(), w);
        ReferenceSwizzle(frame.data() + y * pitch, want.data(), w);
        Expect(got == want, "swizzle matches per-byte reference");
    }

    const uint64_t base = HashVisibleBgra(frame.data(), w, h, pitch);
    Expect(base == HashVisibleBgra(frame.data(), w, h, pitch), "hash deterministic");
    auto padded = frame;
    for (unsigned y = 0; y < h; ++y) for (size_t x = w * 4; x < pitch; ++x) padded[y * pitch + x] ^= 0xFF;
    Expect(HashVisibleBgra(padded.data(), w, h, pitch) == base, "pitch padding ignored");
    for (size_t probe : {size_t(0), size_t(5), size_t(w * 4 - 1), pitch * 3 + 17, pitch * (h - 1) + w * 4 - 2}) {
        auto changed = frame; changed[probe] ^= 1;
        Expect(HashVisibleBgra(changed.data(), w, h, pitch) != base, "single visible byte change detected");
    }
    // Same bytes laid out as different rows must not collide trivially.
    std::vector<uint8_t> flat(w * 4 * h, 0x5A), tall(w * 4 * h, 0x5A);
    Expect(HashVisibleBgra(flat.data(), w * h, 1, w * 4 * h) != HashVisibleBgra(tall.data(), w, h, w * 4), "row structure hashed");

    // Indicative timing at the Xbox 640x480 surface (not an RP6 measurement).
    const unsigned fw = 640, fh = 480; std::vector<uint8_t> big(size_t(fw) * 4 * fh, 0x33), out(size_t(fw) * 4);
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t sink = 0;
    for (int i = 0; i < 50; ++i) {
        big[size_t(i) * 97] ^= 1;
        sink ^= HashVisibleBgra(big.data(), fw, fh, fw * 4);
        for (unsigned y = 0; y < fh; ++y) SwizzleBgraToOpaqueRgba(big.data() + size_t(y) * fw * 4, out.data(), fw);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 50;
    std::printf("Frame present helpers passed (640x480 hash+swizzle %.3f ms/frame, sink %llx)\n", ms, (unsigned long long)(sink & 0xF));
    return 0;
}
