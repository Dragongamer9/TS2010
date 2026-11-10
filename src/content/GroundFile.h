// mapfile.gnd: a route's terrain, split into square sections ("baseboards").
// See docs/formats/route.md.
//
// OUTER STRUCTURE ONLY, worked out from the one route on the disc
// (Builtin\8.ja, kuid 447264 1035). The inside of each section is not decoded
// yet: trainz.exe loads it through two classes chosen by the section's tag
// ('Sec5' -> 0x81ec70, 'Secs' -> 0x8298c0, picked at 0x6d293e), which have
// not been traced.
//
//   "GND\r"  u32 sectionCount  u32 (0)  u32 (4)
//   sectionCount x { char tag[4] (stored reversed: "5ceS" = Sec5), u32 start, u32 end, i32 x, i32 y }
//   ... further header data (meaning unknown) ...
//   per section: bytes [start, end) = section data, then 8192 bytes = 64x64 u16 overview map
//   (sections follow each other with no gaps; the last one ends at the end of the file)
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace trainz::content {

struct GroundSection {
    std::string tag;  // "Sec5" or "Secs"
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t start = 0;  // absolute offset of the section data
    std::uint32_t end = 0;    // absolute offset where the 8192-byte overview map starts
    std::vector<std::uint8_t> data;  // [start, end), not decoded yet
    std::vector<std::uint16_t> overview;  // 64 * 64 values, row by row
};

struct GroundFile {
    std::uint32_t headerWord2 = 0;  // 0 in the sample
    std::uint32_t headerWord3 = 0;  // 4 in the sample
    std::vector<GroundSection> sections;  // in index order
    std::vector<std::uint8_t> extraHeader;  // bytes between the index and the first section (meaning unknown)
    std::vector<std::string> warnings;
};

inline constexpr std::size_t kGroundOverviewSide = 64;
inline constexpr std::size_t kGroundOverviewBytes = kGroundOverviewSide * kGroundOverviewSide * 2;

// Throws ContentError on damage (bad magic, offsets outside the file, sections overlapping).
GroundFile parseGround(std::span<const std::uint8_t> data);

// Overview value to R, G, B. OBSERVED: the colours look right with red and
// blue swapped (BGR 5-6-5); not confirmed.
void overviewToRgb(std::uint16_t v, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b);

}  // namespace trainz::content
