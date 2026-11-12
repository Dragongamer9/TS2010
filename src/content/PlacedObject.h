// Where a placed object stands: the base block shared by the mapfile.obs classes.
// See docs/formats/route-objects.md.
//
// OBSERVED, not traced (the classes' own readers are not traced yet):
//   [class header]  u8 flag   i16 boardX   i16 boardY   f32 x   f32 y   f32 z   f32 yaw   ...
// The class header is empty for mobs (mapobjectstub) and 8 bytes (u32 3, u32
// 0xff000000) for the other classes seen on the disc. x/y are metres inside the
// 720 m baseboard, z the height in metres. On all 50 disc routes the block fits
// 1,760,978 of 1,761,124 objects (146 unexplained); most classes sit on the terrain
// (signals 75-82% within 1 m, junctions 79-88%, crossings 86-93%; mapobjectstub
// 37%), against roughly half that with the terrain rows flipped. The flag is 0 or
// 1 (meaning not known). The float after z is a yaw in RADIANS, not wrapped to
// 0..2pi (-42..154 on the disc): taken mod 90 degrees, 14-45% of non-zero values
// per class lie within 1 degree of a right angle, against 2.2% for random angles
// (objects lined up with streets and track). Why it exceeds one turn is a
// hypothesis only (the editor may keep adding to it). Positive yaw turns
// anticlockwise in x/y (+x toward +y): inferred from buildings beside angled
// track, not traced in the engine; at 0 the object's axes line up with the world's, modulo 90 degrees
// (which side faces +x is not known). Signals and junctions store 0 (they follow the track).
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
    float yaw = 0;  // radians, not wrapped; anticlockwise (+x toward +y); zero axis known only modulo 90 degrees
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

// Where a trackside object (a signal, sgOb) is attached to the track: the record's last 23
// bytes are  u32 2, u32 0, u8 0, i32 piece, f32 distance, u8 direction, 5 bytes (not known).
// OBSERVED on the disc, not traced: with direction 1 the distance runs from the piece's
// `from` vertex, with 0 from its `to` vertex, and the signal then stands within 3 m of that
// point for 11,919 of 12,155 signals (98%; within 6 m 99.6%), beside the track by 1.5-2.5 m
// (the `trackside` offset). Per route the signals keep to one side of the track relative to
// that direction (90% or more on 33 of the 43 routes with 30+ signals: left on the British ECML, right
// on most others), so the direction is read as the direction of travel the signal is for.
// The same layout ends 15,992 tsO2 objects. nullopt if the tail doesn't start 2, 0, 0.
struct TrackAttachment {
    std::int32_t piece = -1;
    float distance = 0;         // metres along the piece, from the end trains enter by
    bool alongPiece = true;     // true: for travel from `from` to `to`
    std::uint8_t tail[5] = {};  // the last 5 bytes (meaning not known)
};
std::optional<TrackAttachment> trackAttachment(const PlacedObject& object);

}  // namespace trainz::content
