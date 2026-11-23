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

// Finds one entry by KUID (any form parseKuidLoose reads: that exact version, else the only
// other installed version) or by name (ignoring case: the only exact name, else the only name
// containing the query). Returns nullptr when nothing or more than one matches; for several,
// `candidates` lists them as "name  <kuid>" (two sessions can share a name, e.g. "Intro").
const CatalogEntry* findEntry(const std::vector<const CatalogEntry*>& entries, const std::string& query,
                              std::vector<std::string>& candidates);

}  // namespace trainz::view
