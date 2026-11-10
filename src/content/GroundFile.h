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
//   world data up to the first section (parseGroundWorld)
//   per section, at start: section data (parseGroundSection); at end: overview map
//   (64 x 64 u16 when version >= 12)
//
// CHECKED: every section of all 50 mapfile.gnd files on the disc (version 13)
// parses to exactly its end offset with parseGroundSection's rules.
#pragma once

#include "Kuid.h"

#include <cstdint>
#include <span>
#include <utility>
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

// One key of the environment block (0x463dd0; 104 bytes, kept at env+0x1e4 + 0x68 * i).
// OBSERVED: the times lie in [0, 1) and the vec4s look like RGBA colours in
// [0, 1] with alpha 1; what each colour is for (sun, ambient, fog...) is a guess.
struct GroundEnvKey {
    float time = 0;
    float colourA[3][4] = {};
    float time2 = 0;
    float colourB[3][4] = {};
};

// Object container (vtable+0x28 = 0x886be0 on the object at world+0x803c).
struct GroundContainerItem {
    std::int32_t index = 0;  // looked up in the container (0x88698b)
    std::string tag;         // class tag, stored reversed like the section tags (e.g. "Rule")
    Kuid kuid;
    std::vector<std::uint8_t> payload;  // object data; the game checks it ends at its size (0x886b8f)
};

// The world data between the index and the first section (file loader
// 0x6d2aa0 from 0x6d2cc7 on). TRACED; CHECKED on all 50 disc mapfile.gnd.
struct GroundWorld {
    // Environment (version >= 7, 0x463dd0).
    std::int32_t envSubVersion = -1;  // -1 when the block starts directly with the key count
    std::vector<GroundEnvKey> envKeys;  // count clamped to 0..32 (0x463e1c)
    Kuid envKuid;                       // -> env+0xee4
    float envFloats[2] = {0, 0};        // -> env object +0xf5c, +0xf84
    bool hasDate = false;               // envSubVersion != -1
    std::int64_t date = 0;              // 64-bit time given to _localtime64 (0x463c10); OBSERVED: Unix seconds
    bool hasEnvExtra = false;           // envSubVersion >= 1
    float envExtra[2] = {0, 0};         // -> env+0xf98, +0xf94
    // Version >= 11: four floats (0x6d2d73 -> world+0x858c). OBSERVED: RGBA-like, alpha 0.8 on every route.
    float colour[4] = {0, 0, 0, 0};
    // Version >= 9: a KUID (0x6d2d82 -> world+0x41a0). Meaning not traced.
    Kuid kuid;
    // Version >= 8: object container (0x886be0).
    std::uint32_t containerVersion = 0;   // the game reads items only for 1..5
    std::uint32_t containerWord = 0;      // read when containerVersion >= 5 (0x886c82); meaning not traced
    std::vector<GroundContainerItem> items;  // containerVersion 3..5 (0x886800)
    std::vector<std::uint8_t> containerTail;  // containerVersion >= 4: u32 size + bytes; skipped by this loader (0x885949)
    // Version < 13: ground-texture table (0x6d2db8): up to 250 x { f32, KUID }.
    std::vector<std::pair<float, Kuid>> textureTable;
};

struct GroundFile {
    std::uint8_t version = 0;  // byte 3 ('\r' = 13 in every disc mapfile.gnd)
    std::vector<GroundSection> sections;  // in index order
    std::vector<std::uint8_t> extraHeader;  // world data: the bytes between the index and the first section
    bool worldDecoded = false;  // parseGround decodes extraHeader into world; on failure it adds a warning
    GroundWorld world;
    std::vector<std::string> warnings;
};

inline constexpr std::size_t kGroundOverviewSide = 64;
inline constexpr std::size_t kGroundOverviewBytes = kGroundOverviewSide * kGroundOverviewSide * 2;
inline constexpr std::size_t kGroundTextureSlots = 250;  // 0xfa, 0x82bfdb
inline constexpr std::size_t kGroundCellSide = 36;       // 0x23 bounds, 0x82c205; 0x510 = 36 * 36 limit, 0x82c02f

// Vertices per side: 76 for Secs (0x1690 = 76 * 76 at 0x82c267), 148 for Sec5
// (0x5590 = 148 * 148 at 0x82166c).
std::size_t groundVertexSide(const std::string& tag);

// Decodes world data (extraHeader) with the rules of the given version. Throws
// ContentError when it runs out, does not end exactly at the first section,
// or uses a container version 2 (items not self-sized; not handled).
GroundWorld parseGroundWorld(std::span<const std::uint8_t> worldData, std::uint8_t version);

// Throws ContentError on damage (offsets outside the file, sections overlapping,
// an unknown tag, a version this reader does not know). World data that does
// not decode is a warning, not an error.
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
