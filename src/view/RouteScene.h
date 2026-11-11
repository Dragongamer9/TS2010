// Builds a 3D scene of part of a route from the decoded files, for SceneRender. It shows
// only what the port can read: terrain heights (coloured from the route's own overview
// maps), track (curves by the port's model in TrackLayout.h), placed objects (as their 3D models when
// AssetMeshes is given and can read them, textured; else dots) and a session's vehicles
// (as models with their bogies, or dots). No terrain textures yet.
#pragma once

#include "content/GroundFile.h"
#include "content/PlacedObject.h"
#include "content/SessionFile.h"
#include "content/TrackLayout.h"
#include "view/AssetMeshes.h"
#include "view/GroundTextures.h"
#include "view/SceneRender.h"

#include <memory>
#include <optional>
#include <vector>

namespace trainz::view {

struct RouteSceneInput {
    const trainz::content::GroundFile* ground = nullptr;     // required
    const trainz::content::TrackLayout* track = nullptr;     // optional
    const std::vector<trainz::content::PlacedObject>* objects = nullptr;  // optional
    const std::vector<trainz::content::Train>* trains = nullptr;          // optional
    AssetMeshes* meshes = nullptr;  // optional: draw objects' 3D models instead of dots
    GroundTextures* groundTextures = nullptr;  // optional: texture the terrain (else overview colours)
};

struct RouteView {
    Scene scene;
    Camera camera;
    double centreX = 0, centreY = 0;  // world metres
    std::size_t sections = 0;
    std::size_t texturedSections = 0;  // sections drawn with baked ground textures
    std::vector<std::unique_ptr<SceneTexture>> ownedTextures;  // the baked ground; scene.textures points here
    std::size_t objectsAsMeshes = 0;
    std::size_t objectsAsDots = 0;
    std::size_t vehiclesAsMeshes = 0;
    std::size_t bogiesAsMeshes = 0;
    std::size_t curvedPieces = 0;  // track pieces drawn as curves (the port's model) rather than straight
    std::size_t vehiclesOnTrack = 0;  // vehicles turned to the modelled track under them
    std::size_t bogiesOnTrack = 0;    // bogies turned to the modelled track under their point  // bogie models drawn on vehicles' "a.bog*" points
};

// How far from a vehicle (or bogie point) the modelled track may be to set its direction and
// height. 97% of disc session vehicles lie within 0.5 m of it, 99.5% within 3 m.
inline constexpr double kOnTrackDistance = 3.0;

inline constexpr std::size_t kMaxSceneTriangles = 4'000'000;  // never exceeded: meshes that don't fit are left out

// `h` or `h + pi`, whichever is nearer to `ref`, as ref + (-pi/2..pi/2): the track's
// direction taken the way round the train runs (front/back come from the train, not the track).
double headingAlong(double h, double ref);

// Whether `triangles` more still fit under kMaxSceneTriangles. Checked before every placed
// object, vehicle body and bogie, so the limit holds however many attachment points a model has.
bool sceneHasRoomFor(const Scene& scene, std::size_t triangles);

// The area within `radius` metres of (centreX, centreY), seen from the south-west and
// above. If no centre is given: the first train if there are trains, otherwise the spot
// with the most track junctions nearby, otherwise the middle of the route.
RouteView buildRouteView(const RouteSceneInput& in, double radius, std::optional<double> centreX,
                         std::optional<double> centreY);

}  // namespace trainz::view
