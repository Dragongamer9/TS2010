// Where a placed object stands: the base block shared by the mapfile.obs classes.
// See docs/formats/route-objects.md.
//
// OBSERVED, not traced (the classes' own readers are not traced yet):
//   [class header]  u8 flag   i16 boardX   i16 boardY   f32 x   f32 y   f32 z   f32 unknown   ...
// The class header is empty for mobs (mapobjectstub) and 8 bytes (u32 3, u32
// 0xff000000) for the other classes seen on the disc. x/y are metres inside the
// 720 m baseboard, z the height in metres. On all 50 disc routes the block fits
// 1,760,978 of 1,761,124 objects (99.99%); most classes sit on the terrain
// (signals 75-82% within 1 m, junctions 79-88%, crossings 86-93%; mapobjectstub
// 37%), against roughly half that with the terrain rows flipped. The flag is 0 or
// 1 (meaning not known); the float after z is not an angle in any obvious unit
// (-42..154 for mobs; 0 for signals and junctions).
#pragma once

#include "content/ObjectContainer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace trainz::content {

struct PlacedObject {
    std::int32_t index = 0;
    std::string tag;       // readable class tag, e.g. "mOBJ"
    Kuid kuid;             // the asset placed
    std::uint8_t flag = 0;     // 0 or 1; meaning not known
    std::int16_t boardX = 0;   // baseboard (terrain section)
    std::int16_t boardY = 0;
    float x = 0;  // metres inside the baseboard, toward +x
    float y = 0;  // metres inside the baseboard, toward +y
    float z = 0;  // height in metres
    float unknown = 0;  // the float after z
    std::vector<std::uint8_t> rest;  // everything after the base block (class-specific, not decoded)

    double worldX() const;
    double worldY() const;
};

// Bytes before the base block for a class tag, or nullopt for classes not seen with
// a position (e.g. tkSt, Rule).
std::optional<std::size_t> placedObjectHeaderSize(const std::string& tag);

// Decodes the base block, or nullopt for other classes, short payloads, a flag other
// than 0/1, or a position that is not finite or not inside 0..720.
std::optional<PlacedObject> decodePlacedObject(const ObjectItem& item);

}  // namespace trainz::content
