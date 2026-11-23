// The game's object container: a list of tagged, self-sized objects. Used by the
// mapfile.gnd world block and by mapfile.obs / mapfile.trk (and the profile.*
// session copies). See docs/formats/route-objects.md.
//
// TRACED in trainz.exe: container reader 0x886be0 (vtable+0x28), item loop
// 0x886800, size limit 0x8869b8, tail 0x8858d0. Class tags are registered with
// the factory at 0x807eb0 (39 registrations; names next to them).
#pragma once

#include "content/Kuid.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trainz::content {

struct ObjectItem {
    std::int32_t index = 0;  // the object's number in its container
    std::string tag;         // class tag in readable order (bytes reversed in the file), e.g. "mOBJ"
    Kuid kuid;               // u32 content, then u32 packed user/version (0x80c8d0)
    std::vector<std::uint8_t> payload;
};

struct ObjectContainer {
    std::uint32_t version = 0;  // the game reads items only for 1..5
    std::uint32_t word = 0;     // read when version >= 5; meaning not traced
    std::vector<ObjectItem> items;
    std::vector<std::uint8_t> tail;  // version >= 4: u32 size + bytes; the game skips them
};

// Reads one container starting at data[pos] and advances pos past it.
// A version outside 1..5 ends the container with no items, as in the game.
// Throws ContentError for version 2 (items are not self-sized; not handled),
// truncated data, or an item over 16 MiB (the game's own limit).
ObjectContainer readObjectContainer(std::span<const std::uint8_t> data, std::size_t& pos);

// The class name registered for a tag (readable order), e.g. "mOBJ" -> "mapobject";
// empty when the registration has no name next to it.
std::string_view objectClassName(std::string_view tag);

struct RouteObjectFile {
    std::uint32_t fileWord = 0;  // first u32: 2 in every .trk, 7..9 in .obs (meaning not traced)
    std::vector<ObjectContainer> containers;  // .trk: 2 (stretches, then vertices); .obs: 1
    std::vector<std::string> warnings;
};

// mapfile.obs / mapfile.trk: a u32, then containers until the end of the file.
// OBSERVED: on the disc, 170 of 172 files read to their exact last byte this way;
// the 2 that don't are the version-10 robescenario route (container version 2).
RouteObjectFile parseRouteObjectFile(std::span<const std::uint8_t> data);

}  // namespace trainz::content
