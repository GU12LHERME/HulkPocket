#include "xbe_image_memory_loader.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace hulkpocket {
namespace {
constexpr std::uint32_t kRetailEntryKey = 0xA8FC57ABu;
constexpr std::uint32_t kDebugEntryKey = 0x94859D4Bu;
constexpr std::uint32_t kRetailThunkKey = 0x5B6D40B6u;
constexpr std::uint32_t kDebugThunkKey = 0xEFB1F152u;

bool Fits(std::uint64_t start, std::uint64_t size, std::uint64_t limit) {
    return start <= limit && size <= limit - start;
}
std::uint32_t Read32(const std::vector<std::uint8_t>& data, std::uint64_t at) {
    if (!Fits(at,4,data.size())) throw std::invalid_argument("XBE field outside file");
    return std::uint32_t(data[at]) | (std::uint32_t(data[at+1]) << 8) |
           (std::uint32_t(data[at+2]) << 16) | (std::uint32_t(data[at+3]) << 24);
}
struct Segment {
    std::uint32_t va, virtual_size, raw, raw_size;
};
}

LoadedXbeImage LoadXbeSections(
    const std::vector<std::uint8_t>& data, XboxGuestAddressSpace& memory) {
    if (data.size() < 0x180 || std::memcmp(data.data(),"XBEH",4) != 0)
        throw std::invalid_argument("invalid Xbox executable signature");
    const auto base = Read32(data,0x104);
    const auto header_size = Read32(data,0x108);
    const auto image_size = Read32(data,0x10C);
    const auto cert = Read32(data,0x118);
    const auto section_count = Read32(data,0x11C);
    const auto section_table = Read32(data,0x120);
    const auto encoded_entry = Read32(data,0x128);
    const auto encoded_thunk = Read32(data,0x158);

    // Guest image is a 32-bit address range. Reject large/malformed
    // image/section declarations before committing any host pages.
    if (!base || !image_size || image_size > 512u*1024u*1024u ||
        !Fits(base,image_size,XboxGuestAddressSpace::kSize) ||
        header_size < 0x180 || header_size > image_size ||
        !Fits(0,header_size,data.size()) ||
        !section_count || section_count > 512 ||
        section_table < base || !Fits(section_table-base,
                                      std::uint64_t(section_count)*56,
                                      header_size))
        throw std::invalid_argument("invalid XBE image or section table");

    const auto inside = [base,image_size](std::uint32_t va) {
        return std::uint64_t(va) >= base &&
               std::uint64_t(va) < std::uint64_t(base)+image_size;
    };
    std::uint32_t entry = 0, thunk = 0;
    if (inside(encoded_entry ^ kRetailEntryKey) &&
        inside(encoded_thunk ^ kRetailThunkKey)) {
        entry = encoded_entry ^ kRetailEntryKey;
        thunk = encoded_thunk ^ kRetailThunkKey;
    } else if (inside(encoded_entry ^ kDebugEntryKey) &&
               inside(encoded_thunk ^ kDebugThunkKey)) {
        entry = encoded_entry ^ kDebugEntryKey;
        thunk = encoded_thunk ^ kDebugThunkKey;
    } else {
        throw std::invalid_argument("unrecognized XBE entry and kernel thunk keys");
    }
    if (cert < base || !Fits(cert-base,12,header_size))
        throw std::invalid_argument("invalid XBE title certificate");
    const auto title = Read32(data,std::uint64_t(cert-base)+8);

    std::vector<Segment> segments;
    segments.reserve(section_count);
    for (std::uint32_t i=0;i<section_count;++i) {
        const auto off = std::uint64_t(section_table-base)+std::uint64_t(i)*56;
        Segment s{Read32(data,off+4),Read32(data,off+8),
                  Read32(data,off+12),Read32(data,off+16)};
        if (!s.virtual_size || s.raw_size > s.virtual_size ||
            s.va < base ||
            !Fits(std::uint64_t(s.va)-base,s.virtual_size,image_size) ||
            !Fits(s.raw,s.raw_size,data.size()))
            throw std::invalid_argument("XBE section exceeds image or source file");
        if (!memory.Base() || !Fits(s.va,s.virtual_size,XboxGuestAddressSpace::kSize))
            throw std::invalid_argument("invalid Xbox guest address");
        segments.push_back(s);
    }
    // Overlapping raw/virtual sections or overlap with the XBE header
    // make the loaded image ambiguous; reject rather than overwrite.
    std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges;
    ranges.emplace_back(base,std::uint64_t(base)+header_size);
    for (const auto& s:segments)
        ranges.emplace_back(s.va,std::uint64_t(s.va)+s.virtual_size);
    std::sort(ranges.begin(),ranges.end());
    for(std::size_t i=1;i<ranges.size();++i)
        if (ranges[i].first < ranges[i-1].second)
            throw std::invalid_argument("overlapping XBE virtual memory regions");

    // The caller must provide a fresh address space. Commit() uses zeroed
    // anonymous pages. Explicit file contents are then copied to guest
    // VAs; virtual trailing bytes remain zero (XBE BSS semantics).
    memory.Commit(base,header_size);
    memory.Write(base,data.data(),header_size);
    for(const auto& s:segments) {
        memory.Commit(s.va,s.virtual_size);
        if(s.raw_size)
            memory.Write(s.va,data.data()+s.raw,s.raw_size);
    }
    LoadedXbeImage loaded{base,image_size,entry,thunk,title,segments.size()};
    loaded.tls_va = Read32(data,0x12C);
    loaded.stack_commit = Read32(data,0x130);
    loaded.heap_reserve = Read32(data,0x134);
    loaded.heap_commit = Read32(data,0x138);
    loaded.init_flags = Read32(data,0x124);
    return loaded;
}
} // namespace hulkpocket
