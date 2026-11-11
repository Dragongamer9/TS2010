// Reader for Trainz asset configuration: config.txt (text) and config.chump (binary).
//
// Both forms hold the same kind of tree: named values (strings, numbers,
// KUIDs, raw bytes) and named groups of further values. See
// docs/formats/config.md for the byte layout and what each rule is based on.
//
//  - config.chump ("ACS$"): header and item checks follow trainz.exe 0x7ff710
//    (header) and 0x7ff1f0 (item walk). Value types are from the data.
//  - config.txt: no symbols for the text parser in trainz.exe, so the grammar
//    is OBSERVED: it parses every config.txt in Base.ja, Data.ja and
//    Builtin\8.ja, but edge cases the game accepts may be missing.
//
// Parsing never reads outside the given bytes and reports bad input as
// ContentError.
#pragma once

#include "Kuid.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trainz::content {

enum class ConfigType : std::uint8_t {
    Group,     // children hold the values
    Integers,  // ints (one or more)
    Floats,    // floats (one or more)
    String,    // text
    Kuid,      // kuid
    Binary,    // bytes (e.g. an embedded JPEG thumbnail)
    Null,      // a key with no value
    Unknown,   // a .chump type byte we don't know; bytes keeps the raw value
};

struct ConfigNode {
    std::string key;
    ConfigType type = ConfigType::Null;
    std::uint8_t chumpType = 0xFF;  // the original type byte in a .chump; 0xFF for config.txt
    std::string text;
    std::vector<std::int32_t> ints;
    std::vector<float> floats;
    trainz::content::Kuid kuid;
    std::vector<std::uint8_t> bytes;
    std::vector<ConfigNode> children;

    // First child with this key, compared ASCII case-insensitively. How the
    // game resolves repeated keys has not been traced.
    const ConfigNode* child(std::string_view name) const;

    // The KUID this value names: a KUID value, or a string holding a KUID in
    // any of the forms parseKuidLoose accepts.
    std::optional<trainz::content::Kuid> asKuid() const;
};

enum class ConfigFormat { Text, Chump };

struct AssetConfig {
    ConfigFormat format = ConfigFormat::Text;
    std::uint32_t chumpVersion = 0;  // header word after "ACS$" (1 in every sample)
    ConfigNode root;                 // type Group, empty key
    std::vector<std::string> warnings;

    // The asset's own KUID (its top-level "kuid" entry), if present and readable. A bare
    // number, or a number written as text, is read as <kuid:-1:number> (observed on 488
    // disc assets; not traced).
    std::optional<trainz::content::Kuid> assetKuid() const;
};

inline constexpr std::size_t kMaxConfigDepth = 64;

AssetConfig parseChump(std::span<const std::uint8_t> data);
AssetConfig parseConfigText(std::span<const std::uint8_t> data);

// Picks the format by the "ACS$" magic.
AssetConfig parseAssetConfig(std::span<const std::uint8_t> data);

// Human-readable text form (for tools and debugging; not byte-identical to the game's files).
std::string formatConfigText(const ConfigNode& root);

}  // namespace trainz::content
