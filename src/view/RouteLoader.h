// Reads what the route view draws, besides the terrain: a route's track (mapfile.trk) and
// placed objects (mapfile.obs), and a session's trains (profile.dat). Shared by route-tool
// and route-viewer. Read-only.
#pragma once

#include "content/JaArchive.h"
#include "content/PlacedObject.h"
#include "content/SessionFile.h"
#include "content/TrackLayout.h"

#include <optional>
#include <string>
#include <vector>

namespace trainz::view {

struct RouteExtras {
    std::optional<trainz::content::TrackLayout> track;   // when the route has a mapfile.trk
    std::vector<trainz::content::PlacedObject> objects;  // from mapfile.obs (empty without one)
    std::optional<std::vector<trainz::content::Train>> trains;  // when a session was given
};

// sessionArchive empty: no session. Throws ContentError on damage or a missing profile.dat.
RouteExtras loadRouteExtras(const trainz::content::JaArchive& archive, const std::string& routeFolder,
                            const std::string& sessionArchive, const std::string& sessionFolder);

}  // namespace trainz::view
