// Driving a train in the generated world: the consist laid along the track behind its lead
// position, one physics step at a time, and a chase camera. No game files.
#pragma once

#include "game/GeneratedWorld.h"
#include "sim/TrackNetwork.h"
#include "sim/TrainPhysics.h"
#include "view/SceneRender.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace trainz::game {

struct ConsistVehicle {
    const trainz::view::PreparedMesh* mesh = nullptr;
    double length = 14;  // metres, coupler to coupler
};

// A self-lapping train brake handle, as TS2010's keys suggest (keyboard.txt:
// train_cabin_brake_application / _lap / _release / _emergency): in Application the brake
// builds up, in Lap it holds, in Release it falls away; Emergency gives the full brake at once.
// The rates are the port's UNCALIBRATED choices, not the game's air-brake model.
struct BrakeHandle {
    enum class Position { Release, Lap, Application, Emergency };
    Position position = Position::Release;
    static constexpr double kApplyPerSecond = 0.15;    // fraction of the full brake
    static constexpr double kReleasePerSecond = 0.25;
    // Moves the brake demand (0..1) for `seconds` at this position.
    void step(double& brake, double seconds) const;
    const char* name() const;
};

// An automatic air brake with the train's own pressures (the enginespec `pressure` container):
// the handle moves the equalising reservoir, the brake pipe follows it (slower on a longer
// train) and the brakes follow the pipe: none while it is charged (`brakepipe`), full service
// when it has fallen to `brakefull`, linearly between; emergency vents the pipe. Pressures are
// in the files' own unit. The RATES are the port's UNCALIBRATED choices (full service in about
// 7 s, release in about 20 s), not the game's model (its flowsize/volume keys are not used yet).
struct AirBrake {
    double charged = 0.00736041;  // brakepipe (the disc's most common value)
    double initial = 0.00693861;  // brakeinitial
    double full = 0.0044992;      // brakefull
    double equaliser = 0.00736041;
    double pipe = 0.00736041;
    // Uses the given pressures when they make sense (full < initial <= charged), else keeps
    // the defaults above; starts charged (brakes off).
    void setPressures(std::optional<double> brakePipe, std::optional<double> brakeInitial, std::optional<double> brakeFull);
    // Moves the equaliser for the handle position and the pipe after it over `seconds`;
    // `cars` sets how fast the pipe follows. Returns the brake demand 0..1.
    double step(BrakeHandle::Position handle, double seconds, std::size_t cars);
    double demand() const;
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
