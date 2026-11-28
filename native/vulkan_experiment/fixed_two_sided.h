#pragma once
// D3D two-sided fixed-function lighting for the Vulkan path.
//
// The pinned XboxXBERecomp FixedVertexHlsl lights only the front face and
// copies it to the back colours (oB0 = oD0). With D3DRS_TWOSIDEDLIGHTING the
// hardware lights back faces with the reversed normal and the back material
// (D3DDevice_SetBackMaterial); the shared pixel shader already selects b0/b1
// on back faces (PixelKey::two_sided). This derives the back computation from
// the generated front block by exact, checked substitutions, so the pinned
// generator stays untouched and any upstream text change fails loudly.
//
// Back material constants travel in DrawConstants::pc[0..4]. pc[] is read
// only by programmable pixel combiners; lit draws are restricted to the fixed
// pixel path (no pixel shader), so the slots are otherwise unused there.
#include <cstring>
#include <regex>
#include <stdexcept>
#include <string>
#include "gpu/shaders.h"
#include "../hulk_state_ext.h"

namespace hulkpocket::vkexp {

inline bool TwoSidedLit(const gpu::State& s) {
    return s.rs[gpu::kRsTwoSidedLighting] && s.rs[gpu::kRsLighting] && !(s.vs && !s.vs->program.empty());
}

namespace detail {
inline size_t FindOnce(const std::string& text, const std::string& needle, const char* what) {
    const auto at = text.find(needle);
    if (at == std::string::npos || text.find(needle, at + 1) != std::string::npos)
        throw std::logic_error(std::string("Two-sided lighting: generator text changed near ") + what);
    return at;
}
inline void ReplaceOnce(std::string& text, const std::string& from, const std::string& to, const char* what) {
    text.replace(FindOnce(text, from, what), from.size(), to);
}
}  // namespace detail

// Requires material colour sources (D3DMCS_MATERIAL): a vertex colour source
// would need its own back-face convention, which is not verified.
inline std::string TwoSidedFixedVertexHlsl(uint16_t regs, bool pretransformed, uint64_t decode, const gpu::FixedKey& key) {
    if (pretransformed || !(key.light & 1)) throw std::invalid_argument("Two-sided lighting requires a lit transformed draw");
    std::string s = gpu::FixedVertexHlsl(regs, pretransformed, decode, key);
    // Effective sources: D3DMCS colour sources fall back to the material
    // when the vertex lacks that colour, so inspect the generated line.
    const auto md_line = s.find("    float4 md = ");
    if (md_line != std::string::npos && s.substr(md_line, s.find('\n', md_line) - md_line).find("i.v") != std::string::npos)
        throw std::invalid_argument("Two-sided lighting with vertex colour material sources");
    const std::string begin_marker = "    float4 md = mat[0], ms = mat[2], ma = mat[1], me = mat[3];\n";
    const auto begin = detail::FindOnce(s, begin_marker, "material sources");
    const auto d1 = detail::FindOnce(s, "    oD1 = ", "specular output");
    const auto end = s.find('\n', d1);
    if (end == std::string::npos || end < begin) throw std::logic_error("Two-sided lighting: malformed lit block");
    std::string back = s.substr(begin + begin_marker.size(), end + 1 - begin - begin_marker.size());
    // The eye vector is shared with the front block; do not redeclare it.
    for (const char* eye : {"    float3 eye = -normalize(vp);\n", "    float3 eye = float3(0, 0, 1);\n"}) {
        const auto at = back.find(eye);
        if (at != std::string::npos) { back.erase(at, std::strlen(eye)); break; }
    }
    if (back.find("float3 eye") != std::string::npos) throw std::logic_error("Two-sided lighting: eye vector not shared");
    detail::ReplaceOnce(back, "    oD0 = ", "    oB0 = ", "diffuse output");
    detail::ReplaceOnce(back, "    oD1 = ", "    oB1 = ", "back specular output");
    std::string power = "mat[4].x";
    for (size_t at; (at = back.find(power)) != std::string::npos;) back.replace(at, power.size(), "pc[4].x");
    static const std::regex ident(R"(\b(md|ms|ma|me|la|ld|ls|vn)\b)");
    back = std::regex_replace(back, ident, "b$1");
    if (back.find("mat[") != std::string::npos) throw std::logic_error("Two-sided lighting: front material left in back block");
    const std::string prologue =
        "    float3 bvn = -vn;\n"
        "    float4 bmd = pc[0], bma = pc[1], bms = pc[2], bme = pc[3];\n";
    detail::ReplaceOnce(s, "    oB0 = oD0;\n    oB1 = oD1;\n", prologue + back, "back colour copy");
    detail::ReplaceOnce(s, "    o.b0 = o.d0;\n    o.b1 = o.d1;\n",
                        "    o.b0 = saturate(oB0);\n    o.b1 = saturate(oB1);\n", "back colour outputs");
    return s;
}

// After CombinerConstants: back material into pc[0..4] for two-sided fixed lighting.
inline void BackMaterialConstants(const gpu::State& s, gpu::DrawConstants& dc) {
    if (!TwoSidedLit(s)) return;
    if (s.ps) throw std::logic_error("Two-sided fixed lighting with a pixel shader");
    if (!HasBackMaterial(s)) throw std::logic_error("Two-sided lighting without observed back material");
    const auto& c = s.transforms[kBackMaterialColorSlot];
    for (unsigned n = 0; n < 4; ++n) for (unsigned j = 0; j < 4; ++j) dc.pc[n][j] = c[n * 4 + j];
    dc.pc[4][0] = s.transforms[kBackMaterialPowerSlot][0];
    dc.pc[4][1] = dc.pc[4][2] = dc.pc[4][3] = 0;
}

}  // namespace hulkpocket::vkexp
