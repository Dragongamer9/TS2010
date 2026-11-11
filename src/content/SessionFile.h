// Session files: profile.dat (the session's rules, trains and library state).
// See docs/formats/session.md.
//
// OBSERVED on all 138 disc profile.dat, not traced:
//   u32 version (0..3)
//   blocks: { u32 size, u8 data[size] }, 3 for version 0, 4 for version 1, 5 for 2 and 3
//   version 3: a 44-byte trailer (not decoded)
// Blocks holding object containers (ObjectContainer.h) carry BHVR (behaviours,
// i.e. rules), trCt (probably trains; not decoded) and gslb (library state);
// the other blocks are not decoded. profile.obs / profile.trk use the
// mapfile.obs / .trk layout (RouteObjectFile).
#pragma once

#include "content/AssetConfig.h"
#include "content/ObjectContainer.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace trainz::content {

struct SessionBlock {
    std::vector<std::uint8_t> bytes;
    std::optional<ObjectContainer> container;  // set when the whole block reads as one container
};

struct SessionProfile {
    std::uint32_t version = 0;
    std::vector<SessionBlock> blocks;
    std::vector<std::uint8_t> trailer;  // version 3
    std::vector<std::string> warnings;
};

// Throws ContentError for an unknown version or blocks that run past the end.
SessionProfile parseSessionProfile(std::span<const std::uint8_t> data);

// A behaviour (rule) item: its name and its settings, an embedded .chump.
//   version 3: u32 3, u32 size, u32 1, u8 0, u32 nameLength, name, u32 0, chump
//   version 2: u32 2, u32 size, u32 nameLength, name, u32 0, chump
// (4,251 of 4,254 disc behaviours fit; the chump ends exactly at the payload's end.)
struct Behaviour {
    std::int32_t index = 0;
    Kuid kuid;         // the rule asset
    std::uint32_t version = 0;
    std::string name;  // UTF-8, e.g. "Startup Options"
    AssetConfig settings;
};

// nullopt if the item is not BHVR or does not fit either layout.
std::optional<Behaviour> decodeBehaviour(const ObjectItem& item);

// A train (trCt, magic 0xcaffa1da, version 5): its place on the track and its vehicles.
// Observed on all 655 version-5 disc trains (3,363 vehicles), not traced:
//   u32 magic   u32 5   u32 vehicleCount   u32 0   i32 trackPiece   f32 distance   u32 direction
//   vehicles: { u32 content, u32 packed user/version, u32 0, u8 0, u32 size, record[size] }
//   tail: FEEDCEEB ... (with an embedded .chump; not decoded)
// A vehicle record: u32 8 or 9, u32 3, u32 (…ff; perhaps a colour), the placed-object
// position block (flag, board, x, y, z, yaw) and, at offset 41, its name
// (u8 0x80, u32 length, text). Older trains (826 on the disc, from version-1 sessions)
// use another layout and are not decoded.
struct Vehicle {
    Kuid kuid;
    std::string name;  // e.g. "CN SD40-2 1"
    std::uint32_t recordVersion = 0;
    std::uint8_t flag = 0;
    std::int16_t boardX = 0;
    std::int16_t boardY = 0;
    float x = 0;
    float y = 0;
    float z = 0;
    float yaw = 0;
    std::vector<std::uint8_t> record;  // the whole record (mostly not decoded)
};

struct Train {
    std::int32_t index = 0;
    // Checked on the 25 trains of "Midday Manifest" against its route (kuid:107727:1019):
    // every trackPiece is a tkSt of the route, the first vehicle stands on that
    // piece's line (0.0 m off), and distance is the metres from one end of it.
    // That is one session; which end, and placement on curved track, are not established.
    std::int32_t trackPiece = 0;
    float distance = 0;
    std::uint32_t direction = 0;  // perhaps which end distance counts from (not checked)
    std::vector<Vehicle> vehicles;
    std::vector<std::uint8_t> tail;
};

// nullopt if the item is not trCt, not version 5, or a vehicle runs past the end.
std::optional<Train> decodeTrain(const ObjectItem& item);

}  // namespace trainz::content
