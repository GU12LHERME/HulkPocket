// Extracted from BLACK Android native/vk_shader.cpp, 8e9c6fc7d570503ea68d039cff16c09339ee6d30.
// MIT license: see LICENSE in this directory. No renderer or cache imported.
#include "hlsl_adapter.h"
#include <regex>
#include <vector>
namespace hulkpocket::upstream_probe {
struct Param {
    std::string type, name;
};

bool Opaque(const std::string& type) { return type.find("Texture") != std::string::npos || type.find("Sampler") != std::string::npos; }

std::vector<Param> Params(const std::string& list) {
    std::vector<Param> out;
    static const std::regex one(R"(\s*(.*?)\s*(\w+)\s*(,|$))");
    for (auto it = std::sregex_iterator(list.begin(), list.end(), one); it != std::sregex_iterator(); ++it)
        if ((*it)[2].length()) out.push_back({(*it)[1], (*it)[2]});
    return out;
}

// the index past the brace that closes the one at open
size_t CloseOf(const std::string& s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size(); i++) {
        if (s[i] == '{') depth++;
        if (s[i] == '}' && --depth == 0) return i + 1;
    }
    return std::string::npos;
}

// glslang without the optimizer hands a texture or sampler parameter over through a function variable, which
// Vulkan's SPIR-V forbids (a store to an opaque type, VUID 06924). Adreno drivers run it all the same; Mali's
// sample garbage through it, which the glow passes do with the SDK's spreadtap and shadow maps with its shadow:
// the glow came out saturated and flickering (issues 4 and 20). Each helper taking them is replaced by a copy per
// call site, named for the globals passed and reading them directly, declared after them, before main
std::string Specialize(const std::string& hlsl) {
    static const std::regex def(R"(\n(\w[\w<>]*)[ \t]+(\w+)\(([^()]*)\)\s*\{)");
    std::string s = hlsl, made;
    size_t from = 0;
    for (std::smatch m; std::regex_search(s.cbegin() + long(from), s.cend(), m, def);) {
        size_t start = from + size_t(m.position(0)), open = start + size_t(m.length(0)) - 1, end = CloseOf(s, open);
        std::vector<Param> params = Params(m[3]);
        size_t opaque = 0;
        while (opaque < params.size() && Opaque(params[opaque].type)) opaque++;
        if (!opaque || end == std::string::npos) {
            from = open;
            continue;
        }
        std::string type = m[1], name = m[2], body = s.substr(open, end - open), rest;
        for (size_t i = opaque; i < params.size(); i++) rest += (i > opaque ? ", " : "") + params[i].type + " " + params[i].name;
        s.erase(start, end - start);
        // the calls: name(global, global, ...) with one identifier per opaque parameter
        std::string pattern = "\\b" + name + "\\(";
        for (size_t i = 0; i < opaque; i++) pattern += R"(\s*(\w+)\s*,)";
        const std::regex call(pattern);
        std::string out;
        size_t at = 0;
        for (std::smatch c; std::regex_search(s.cbegin() + long(at), s.cend(), c, call);) {
            std::string spec = name, copy = body;
            for (size_t i = 0; i < opaque; i++) {
                spec += "_" + c[i + 1].str();
                copy = std::regex_replace(copy, std::regex("\\b" + params[i].name + "\\b"), c[i + 1].str());
            }
            std::string decl = "\n" + type + " " + spec + "(" + rest + ") " + copy + "\n";
            if (made.find(decl) == std::string::npos) made += decl;
            out += s.substr(at, size_t(c.position(0))) + spec + "(";
            at += size_t(c.position(0)) + size_t(c.length(0));
        }
        s = out + s.substr(at);
    }
    if (made.empty()) return hlsl;
    // the pixel shader's entry point follows the texture and sampler declarations
    size_t main = s.rfind("\nfloat4 main(");
    if (main == std::string::npos) return hlsl;
    return s.insert(main, made);
}

// fxc's spellings glslang does not read: 1.#INF for infinity
std::string Adapt(const std::string& hlsl) {
    std::string s = Specialize(hlsl);
    for (size_t at; (at = s.find("1.#INF")) != std::string::npos;) s.replace(at, 6, "asfloat(0x7F800000u)");
    // the vertex inputs at the location of their register, ATTRn: the pipeline's attributes are numbered so
    static const std::regex attr(R"((\n[ \t]*)(\w+ v\d+ : ATTR(\d+);))");
    return std::regex_replace(s, attr, "$1[[vk::location($3)]] $2");
}

}
