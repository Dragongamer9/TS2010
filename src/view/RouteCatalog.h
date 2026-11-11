// Routes and sessions of an install, by name: for choosing what route-viewer shows without
// typing archive names and folders. Routes are assets of kind "map"; sessions are kind
// "profile" and name their route with "map-kuid" (observed on the disc). Read-only.
#pragma once

#include "content/AssetIndex.h"
#include "content/Kuid.h"

#include <string>
#include <vector>

namespace trainz::view {

struct CatalogEntry {
    trainz::content::Kuid kuid;
    std::string name;  // the asset's username (its KUID when it has none)
    trainz::content::AssetLocation where;
};

struct CatalogRoute {
    CatalogEntry route;
    std::vector<CatalogEntry> sessions;  // sorted by name
};

// Every route in an archive (sorted by name) with its sessions. Sessions whose route is not
// in the index, and assets in plain folders (not archives), are left out. Configs that
// can't be read are skipped.
std::vector<CatalogRoute> listRoutes(const trainz::content::AssetIndex& index);

// Finds one entry by KUID (any form parseKuidLoose reads; any version) or by name: an exact
// name (ignoring case) wins, else the only name containing the query. Returns nullptr and
// fills `candidates` (names) when nothing or more than one matches.
const CatalogEntry* findEntry(const std::vector<const CatalogEntry*>& entries, const std::string& query,
                              std::vector<std::string>& candidates);

}  // namespace trainz::view
