#pragma once
// HulkPocket extensions carried inside the pinned gpu::State without
// changing its layout. gpu::State::transforms has 512 slots indexed by the
// bridge's D3DTRANSFORMSTATETYPE mapping (t<2 -> 2..3, t<6 -> 16..19,
// otherwise t+250 -> 256..259; t>=10 is rejected), so slots 8 and 9 are never
// written by guest transforms and are reserved here.
#include <array>
#include <cmath>
#include <cstring>
#include "gpu/state.h"

namespace hulkpocket {

inline constexpr unsigned kBackMaterialColorSlot = 8;  // diffuse, ambient, specular, emissive (RGBA each)
inline constexpr unsigned kBackMaterialPowerSlot = 9;  // [0] power, [1] 1 when observed from the guest

// D3DDevice_SetBackMaterial record (D3DMATERIAL8 layout, 17 floats).
inline bool StoreBackMaterial(gpu::State& s, const std::array<float, 17>& m) {
    for (float f : m) if (!std::isfinite(f)) return false;
    if (m[16] < 0) return false;
    std::memcpy(s.transforms[kBackMaterialColorSlot].data(), m.data(), 16 * sizeof(float));
    s.transforms[kBackMaterialPowerSlot].fill(0);
    s.transforms[kBackMaterialPowerSlot][0] = m[16];
    s.transforms[kBackMaterialPowerSlot][1] = 1;
    return true;
}

inline bool HasBackMaterial(const gpu::State& s) {
    return s.transforms[kBackMaterialPowerSlot][1] == 1;
}

}  // namespace hulkpocket
