// mapfile.gnd: a route's terrain, split into square sections ("baseboards").
// See docs/formats/route.md.
//
// TRACED in trainz.exe (objdump; addresses are VAs):
//   file loader 0x6d2aa0, section index entry 0x827230 (Secs) / 0x81acf0 (Sec5),
//   section data 0x82be00 (Secs) / 0x821200 (Sec5), vertex 0x6f0e50,
//   overview map 0x6e4260 -> 0x6e4070.
//
//   "GND" (3 bytes, not checked by the game)  u8 version (1..13)  u32 sectionCount
//   sectionCount x { i32 x, i32 y, [char tag[4], stored reversed, if version >= 12], u32 start, u32 end }
//   ... world data (environment, ground-texture table, ...) up to the first section; not decoded ...
//   per section, at start: section data (parseGroundSection); at end: overview map
//   (64 x 64 u16 when version >= 12)
//
// CHECKED: every section of all 50 mapfile.gnd files on the disc (version 13)
// parses to exactly its end offset with parseGroundSection's rules.
#pragma once

#include "Kuid.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace trainz::content {

struct GroundSection {
    std::string tag;  // "Sec5" or "Secs" ("Secs" when the file has no tags, version < 12)
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t start = 0;  // absolute offset of the section data
    std::uint32_t end = 0;    // absolute offset of the overview map
    std::vector<std::uint8_t> data;  // [start, end); decode with parseGroundSection
    std::vector<std::uint16_t> overview;  // 64 * 64 values, row by row
};

struct GroundFile {
    std::uint8_t version = 0;  // byte 3 ('\r' = 13 in every disc mapfile.gnd)
    std::vector<GroundSection> sections;  // in index order
    std::vector<std::uint8_t> extraHeader;  // world data between the index and the first section (not decoded)
    std::vector<std::string> warnings;
};

inline constexpr std::size_t kGroundOverviewSide = 64;
inline constexpr std::size_t kGroundOverviewBytes = kGroundOverviewSide * kGroundOverviewSide * 2;
inline constexpr std::size_t kGroundTextureSlots = 250;  // 0xfa, 0x82bfdb
inline constexpr std::size_t kGroundCellSide = 36;       // 0x23 bounds, 0x82c205; 0x510 = 36 * 36 limit, 0x82c02f

// Vertices per side: 76 for Secs (0x1690 = 76 * 76 at 0x82c267), 148 for Sec5
// (0x5590 = 148 * 148 at 0x82166c).
std::size_t groundVertexSide(const std::string& tag);

// Throws ContentError on damage (offsets outside the file, sections overlapping,
// an unknown tag, a version this reader does not know).
GroundFile parseGround(std::span<const std::uint8_t> data);

struct GroundTextureSlot {
    float weight = 0;   // read for every slot; a KUID follows only when weight > 0 (comiss/jbe, 0x82bf68)
    bool used = false;  // weight > 0
    Kuid kuid;          // file order: u32 content, u32 packed user/version (reader 0x80c8d0)
};

struct GroundHeightCell {
    std::uint16_t col = 0;  // first u16 (0x82c0aa); the game ignores cells with col or row > 35
    std::uint16_t row = 0;
    float value = 0;  // meaning not traced
};

struct GroundVertex {
    std::uint8_t texture[3] = {0xFF, 0xFF, 0xFF};  // index into the section's texture slots, 0xFF = none
    std::uint8_t weight[3] = {0, 0, 0};            // 0xFF for the single-texture short forms
    std::uint8_t extra[3] = {0, 0, 0};             // third per-layer byte; meaning not traced
    float height = 0;
    std::uint8_t layersInFile = 0;  // layers stored (only the first 3 are kept, as in the game)
};

struct GroundSectionData {
    std::uint32_t textureSlotCount = 0;  // as stored (250 on the disc); at most 250 slots are read
    std::vector<GroundTextureSlot> textures;
    std::vector<GroundHeightCell> cells;
    std::vector<GroundVertex> vertices;  // groundVertexSide(tag)^2, row by row (order of rows/columns not checked)
};

// Decodes one section's data with the rules of the given file version.
// Throws ContentError when the data runs out or does not end exactly at the
// section's end (the game itself does not check that).
GroundSectionData parseGroundSection(const GroundSection& section, std::uint8_t version);

// Overview value to R, G, B. The game expands bits 11-15 into its first
// colour byte, 5-10 into the second, 0-4 into the third (0x6e4070, traced);
// which byte is red depends on the bitmap format (not traced). OBSERVED: the
// maps look right with bits 0-4 as red (what this function does).
void overviewToRgb(std::uint16_t v, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b);

}  // namespace trainz::content
