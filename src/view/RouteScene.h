// Builds a 3D scene of part of a route from the decoded files, for SceneRender. It shows
// only what the port can read: terrain heights (coloured from the route's own overview
// maps), track as straight lines between vertices, placed objects (as their 3D models when
// AssetMeshes is given and can read them, textured; else dots) and a session's vehicles
// (as models too, or dots). No terrain textures or track curves yet.
#pragma once

#include "content/GroundFile.h"
#include "content/PlacedObject.h"
#include "content/SessionFile.h"
#include "content/TrackLayout.h"
#include "view/AssetMeshes.h"
#include "view/SceneRender.h"

#include <optional>
#include <vector>

namespace trainz::view {

struct RouteSceneInput {
    const trainz::content::GroundFile* ground = nullptr;     // required
    const trainz::content::TrackLayout* track = nullptr;     // optional
    const std::vector<trainz::content::PlacedObject>* objects = nullptr;  // optional
    const std::vector<trainz::content::Train>* trains = nullptr;          // optional
    AssetMeshes* meshes = nullptr;  // optional: draw objects' 3D models instead of dots
};

struct RouteView {
    Scene scene;
    Camera camera;
    double centreX = 0, centreY = 0;  // world metres
    std::size_t sections = 0;
    std::size_t objectsAsMeshes = 0;
    std::size_t objectsAsDots = 0;
    std::size_t vehiclesAsMeshes = 0;
};

inline constexpr std::size_t kMaxSceneTriangles = 4'000'000;

// The area within `radius` metres of (centreX, centreY), seen from the south-west and
// above. If no centre is given: the first train if there are trains, otherwise the spot
// with the most track junctions nearby, otherwise the middle of the route.
RouteView buildRouteView(const RouteSceneInput& in, double radius, std::optional<double> centreX,
                         std::optional<double> centreY);

}  // namespace trainz::view
