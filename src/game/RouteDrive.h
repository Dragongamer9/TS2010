// Driving a session's train on its real route (read from the original files, read-only):
// where the train stands, and drawing its own vehicle and bogie models along the track.
#pragma once

#include "content/SessionFile.h"
#include "sim/TrackNetwork.h"
#include "view/AssetMeshes.h"
#include "view/SceneRender.h"

#include <array>
#include <optional>
#include <vector>

namespace trainz::game {

// The first vehicle's place on the track, facing the way the train runs: away from its second
// vehicle (vehicles are stored front to back; with one vehicle, toward the piece's `to` end).
// Found from the stored positions (not the stored trackPiece/direction, whose meaning is not
// fully known). nullopt if the first vehicle is more than maxDistance from any track.
std::optional<trainz::sim::TrackPosition> trainStart(const trainz::sim::TrackNetwork& net, const trainz::content::Train& train,
                                                     double maxDistance = 5.0);

struct RouteVehicle {
    const trainz::view::PreparedMesh* body = nullptr;
    std::array<const trainz::view::PreparedMesh*, 2> bogies = {nullptr, nullptr};  // on a.bog0, a.bog1 (may be null)
    std::array<double, 2> bogieY = {0, 0};  // the points' positions along the body (+y forward)
    bool onBogies = false;                  // both points known
    double length = 14;                     // along the body, from its model
};

// A vehicle from its model: its length (the model's extent along y) and bogie points.
RouteVehicle routeVehicle(const trainz::view::PreparedMesh& body, const trainz::view::PreparedMesh* bogie0,
                          const trainz::view::PreparedMesh* bogie1);

// Draws the vehicles along the track, the first one's middle at `front`, each following the
// one before (`gap` metres apart). Returns how many were drawn.
int addRouteTrain(trainz::view::Scene& scene, const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& front,
                  const std::vector<RouteVehicle>& vehicles, double gap = 0.5);

}  // namespace trainz::game
