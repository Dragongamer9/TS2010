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

}  // namespace trainz::content
