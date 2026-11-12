// Driving a train in the generated world: the consist laid along the track behind its lead
// position, one physics step at a time, and a chase camera. No game files.
#pragma once

#include "game/GeneratedWorld.h"
#include "sim/TrackNetwork.h"
#include "sim/TrainPhysics.h"
#include "view/SceneRender.h"

#include <vector>

namespace trainz::game {

struct ConsistVehicle {
    const trainz::view::PreparedMesh* mesh = nullptr;
    double length = 14;  // metres, coupler to coupler
};

struct Drive {
    trainz::sim::TrackPosition lead;  // the front of the first vehicle, facing the way it drives
    double speed = 0;                 // m/s along the train's facing (negative: backing up)
    trainz::sim::Controls controls;
    trainz::sim::TrainSpec spec;
    bool atBuffer = false;  // ran into a line end on the last step

    // One step of `seconds`: physics with the grade under the lead, then the move along the
    // track; a line end stops the train.
    void step(const trainz::sim::TrackNetwork& net, double seconds);
};

// Places every vehicle and its two bogies along the track behind `lead` (bogies one third of
// the vehicle's length from its middle, the body between them). Returns how many were placed.
int addConsist(trainz::view::Scene& scene, const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& lead,
               const std::vector<ConsistVehicle>& vehicles, const trainz::view::PreparedMesh& bogie, double gap = 1.0);

// A camera `back` metres behind the lead along the track, `side` metres to its left and `up`
// metres above, looking at a point `ahead` metres before the lead.
trainz::view::Camera chaseCamera(const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& lead,
                                 double back = 95, double up = 16, double side = 12, double ahead = 10);

}  // namespace trainz::game
