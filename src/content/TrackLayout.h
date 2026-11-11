// Track geometry from mapfile.trk: vertices (tkVx) and the stretches (tkSt)
// joining them. See docs/formats/route-objects.md.
//
// OBSERVED, not traced (the classes' own readers are not traced yet):
// - tkVx: u32 version (3 on all 406,207 disc vertices), i16 boardX, i16 boardY,
//   f32 x, f32 y, f32 z, then 21 bytes not decoded. x/y are metres inside a
//   720 m baseboard and z is the height in metres: on the disc, 99.5% of
//   vertices fall inside a terrain section and 66% lie within 1 m of the ground
//   there (with rows flipped instead: 39%).
// - tkSt: u32 version (6 or 4), i32 from, i32 to (vertex indices), then the
//   track asset's KUID (always equal to the item's own), then 4..10 bytes not
//   decoded. Both ends exist on all 293,750 disc stretches.
#pragma once

#include "content/Kuid.h"
#include "content/ObjectContainer.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace trainz::content {

inline constexpr double kBaseboardSize = 720.0;  // metres (observed: local x/y range, 72 cells x 10 m)

struct TrackVertex {
    std::int32_t index = 0;  // the item's index; stretches refer to it
    std::uint32_t version = 0;
    std::int16_t boardX = 0;  // the terrain section (baseboard) it lies on
    std::int16_t boardY = 0;
    float x = 0;  // metres inside the baseboard, toward +x
    float y = 0;  // metres inside the baseboard, toward +y
    float z = 0;  // height in metres
    std::vector<std::uint8_t> rest;  // not decoded

    double worldX() const { return boardX * kBaseboardSize + x; }
    double worldY() const { return boardY * kBaseboardSize + y; }
};

struct TrackStretch {
    std::int32_t index = 0;
    std::uint32_t version = 0;
    std::int32_t from = 0;  // vertex indices
    std::int32_t to = 0;
    Kuid track;             // the track asset
    std::vector<std::uint8_t> rest;  // not decoded
};

struct TrackLayout {
    std::map<std::int32_t, TrackVertex> vertices;
    std::vector<TrackStretch> stretches;
    std::size_t otherItems = 0;  // e.g. tkBr (not decoded)
    std::vector<std::string> warnings;
};

// Decodes one item, or nullopt if it is not that class or too short.
std::optional<TrackVertex> decodeTrackVertex(const ObjectItem& item);
std::optional<TrackStretch> decodeTrackStretch(const ObjectItem& item);

// Collects every vertex and stretch of a parsed mapfile.trk. Stretches whose ends
// are missing, duplicate vertex indices and unreadable items become warnings.
TrackLayout readTrackLayout(const RouteObjectFile& trk);

}  // namespace trainz::content
