// Asset index: which assets exist and where each one lives.
//
// Built read-only from .ja archives (Base.ja, Data.ja, Builtin/*.ja) and loose
// folders (UserData/local). An asset is a folder holding config.txt or
// config.chump; configs nested inside another asset's folder are that asset's
// sub-parts (e.g. ".../train_brake_lever/config.txt") and are not assets of
// their own. The KUID comes from the config itself; the folder name
// (hash-XX/kuid ...) is only used as a cross-check.
//
// What is NOT decided here, because the game's rules have not been traced:
//   - which copy wins when the same KUID exists in several places
//     (built-in vs local vs editing), and
//   - which version is used when another asset asks for an older KUID.
// The index reports every candidate; see docs/formats/asset-index.md.
#pragma once

#include "AssetConfig.h"
#include "JaArchive.h"
#include "Kuid.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace trainz::content {

struct AssetLocation {
    enum class Container { Archive, Folder };
    Container container = Container::Archive;
    std::filesystem::path source;  // the .ja file, or the root folder that was scanned
    std::string folder;            // asset folder inside it ("" = top level), '/' separated
    std::string configName;        // "config.txt" or "config.chump"
};

struct AssetRecord {
    Kuid kuid;                  // normalized
    std::string kind;           // e.g. "scenery", "traincar", "behavior" (empty if missing)
    std::string username;       // display name (empty if missing)
    std::vector<Kuid> obsoletes;  // KUIDs listed in its obsolete-table
    AssetLocation where;
};

class AssetIndex {
public:
    // Adds every asset in an open archive. Reads only the config entries, in file order.
    void addArchive(const JaArchive& archive);

    // Adds every asset found below a folder (read-only directory walk, at most 6 levels deep).
    void addFolder(const std::filesystem::path& root);

    // Base.ja, Data.ja, Builtin/*.ja and UserData/local under an install folder
    // (names matched case-insensitively). Missing parts are noted in warnings().
    static AssetIndex scanInstall(const std::filesystem::path& installRoot);

    const std::vector<AssetRecord>& records() const { return records_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

    // Every copy of exactly this KUID (normalized), in the order the sources were added.
    std::vector<const AssetRecord*> find(const Kuid& kuid) const;

    // Every version of user:content (plain kuid and all kuid2 versions), lowest version first.
    std::vector<const AssetRecord*> allVersions(std::int32_t user, std::int32_t content) const;

    // Assets whose obsolete-table lists this KUID.
    std::vector<const AssetRecord*> obsoletedBy(const Kuid& kuid) const;

private:
    struct ParsedConfig {
        std::string folder;
        std::string entryName;  // for messages
        bool valid = false;
        AssetRecord record;
        std::string error;
    };
    // Keeps the valid configs that are not inside another VALID asset's folder.
    void commit(std::vector<ParsedConfig>& parsed, const std::string& sourceName);
    void addRecord(AssetRecord rec, const std::string& folderName);
    void warn(std::string message);

    std::vector<AssetRecord> records_;
    std::multimap<std::pair<std::int32_t, std::int32_t>, std::size_t> byUserContent_;
    std::vector<std::string> warnings_;
    std::size_t warningCount_ = 0;
};

// Picks one config per folder out of a list of '/'-separated file paths
// (config.chump when a folder has both: an assumption, see the doc).
std::vector<std::pair<std::string, std::string>> selectConfigCandidates(const std::vector<std::string>& paths,
                                                                        std::vector<std::string>* notes = nullptr);

// selectConfigCandidates, then drops every folder inside another candidate's
// folder (treating every candidate as a valid asset). The index itself only
// lets VALID parents hide nested configs. Exposed for tests.
std::vector<std::pair<std::string, std::string>> selectAssetConfigs(const std::vector<std::string>& paths,
                                                                    std::vector<std::string>* notes = nullptr);

}  // namespace trainz::content
