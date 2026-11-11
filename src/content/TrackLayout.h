// Track geometry from mapfile.trk: vertices (tkVx) and the stretches (tkSt)
// joining them. See docs/formats/route-objects.md.
//
// OBSERVED, not traced (the classes' own readers are not traced yet):
// - tkVx: u32 version (3 on all 406,207 disc vertices), i16 boardX, i16 boardY,
//   f32 x, f32 y, f32 z, then 4 link slots {i32 item (-1 = empty), u8 flag} and
//   one more byte. The slots list every track piece (tkSt or tkBr) that ends at
//   the vertex: 587,479 of 587,500 disc stretch ends are listed. The flag's
//   meaning is unknown (it is not simply "from" or "to"). x/y are metres inside a
//   720 m baseboard and z is the height in metres: on the disc, 99.5% of
//   vertices fall inside a terrain section and 66% lie within 1 m of the ground
//   there (with rows flipped instead: 39%).
// - tkSt: u32 version (6 or 4), i32 from, i32 to (vertex indices), then the
//   track asset's KUID (always equal to the item's own), then 4..10 bytes not
//   decoded (no floats: how a stretch curves is not stored there). Both ends exist
//   on all 293,750 disc stretches. tkBr (88/94 bytes) is u32 2 followed by a
//   whole stretch, then more fields not decoded; its ends are the vertices that
//   link to it.
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

struct TrackLink {
    std::int32_t item = -1;  // index of a tkSt or tkBr in the first container
    std::uint8_t flag = 0;   // meaning unknown
};

struct TrackVertex {
    std::int32_t index = 0;  // the item's index; stretches refer to it
    std::uint32_t version = 0;
    std::int16_t boardX = 0;  // the terrain section (baseboard) it lies on
    std::int16_t boardY = 0;
    float x = 0;  // metres inside the baseboard, toward +x
    float y = 0;  // metres inside the baseboard, toward +y
    float z = 0;  // height in metres
    std::vector<TrackLink> links;  // the non-empty slots, in order (1 = line end, 2 = along a line, 3+ = junction?)
    std::uint8_t lastByte = 0;     // the byte after the 4 slots (0, 1 or 2 on the disc; meaning unknown)
    std::vector<std::uint8_t> rest;  // anything after that (empty in every disc file)

    double worldX() const { return boardX * kBaseboardSize + x; }
    double worldY() const { return boardY * kBaseboardSize + y; }
};

struct TrackStretch {
    std::string kind = "tkSt";  // "tkSt", or "tkBr": a stretch wrapped in one more word (meaning not known)
    std::uint32_t wrapperVersion = 0;  // tkBr only: its own first word (2 on the disc)
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
    std::size_t otherItems = 0;  // items that are neither vertices nor tkSt/tkBr
    std::vector<std::string> warnings;
};

// Decodes one item, or nullopt if it is not that class or too short.
std::optional<TrackVertex> decodeTrackVertex(const ObjectItem& item);
std::optional<TrackStretch> decodeTrackStretch(const ObjectItem& item);

// Collects every vertex and stretch (tkSt and tkBr) of a parsed mapfile.trk.
// Stretches whose ends are missing, vertex links to unknown pieces, duplicate
// vertex indices and unreadable items become warnings.
TrackLayout readTrackLayout(const RouteObjectFile& trk);

}  // namespace trainz::content
