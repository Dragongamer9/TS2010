#include "Drive.h"

#include <algorithm>

#include "view/AssetMeshes.h"

#include <cmath>
#include <numbers>

namespace trainz::game {

void BrakeHandle::step(double& brake, double seconds) const {
    if (!std::isfinite(brake)) {
        brake = 0;
    }
    const double dt = std::isfinite(seconds) && seconds > 0 ? std::min(seconds, 10.0) : 0.0;
    switch (position) {
    case Position::Release:
        brake -= kReleasePerSecond * dt;
        break;
    case Position::Lap:
        break;
    case Position::Application:
        brake += kApplyPerSecond * dt;
        break;
    case Position::Emergency:
        brake = 1;
        break;
    }
    brake = std::clamp(brake, 0.0, 1.0);
}

void AirBrake::setPressures(std::optional<double> brakePipe, std::optional<double> brakeInitial, std::optional<double> brakeFull) {
    if (brakePipe && brakeFull && *brakeFull > 0 && *brakeFull < *brakePipe) {
        charged = *brakePipe, full = *brakeFull;
        initial = brakeInitial && *brakeInitial > full && *brakeInitial <= charged ? *brakeInitial : charged - 0.15 * (charged - full);
    }
    equaliser = pipe = charged;
}

double AirBrake::demand() const {
    if (!(charged > full)) {
        return 0;
    }
    if (pipe < 0.5 * full) {
        return 1;  // vented: emergency
    }
    return std::clamp((charged - pipe) / (charged - full), 0.0, 1.0);
}

double AirBrake::step(BrakeHandle::Position handle, double seconds, std::size_t cars) {
    const double dt = std::isfinite(seconds) && seconds > 0 ? std::min(seconds, 10.0) : 0.0;
    const double span = charged - full;
    switch (handle) {
    case BrakeHandle::Position::Application:
        // At least the initial reduction at once, then on toward full service.
        equaliser = std::min(equaliser, initial);
        equaliser = std::max(full, equaliser - span / 7.0 * dt);
        break;
    case BrakeHandle::Position::Lap:
        break;
    case BrakeHandle::Position::Release:
        equaliser = std::min(charged, equaliser + span / 20.0 * dt);
        break;
    case BrakeHandle::Position::Emergency:
        equaliser = 0;
        break;
    }
    // The pipe follows the equaliser (at once in an emergency, which vents it at every car).
    const double tau = 1.0 + 0.05 * static_cast<double>(cars);
    pipe = handle == BrakeHandle::Position::Emergency ? 0.0 : pipe + (equaliser - pipe) * std::min(1.0, dt / tau);
    if (!std::isfinite(pipe)) {
        pipe = charged;
    }
    return demand();
}

const char* BrakeHandle::name() const {
    switch (position) {
    case Position::Release:
        return "release";
    case Position::Lap:
        return "lap";
    case Position::Application:
        return "application";
    case Position::Emergency:
        return "emergency";
    }
    return "?";
}

using namespace trainz::sim;
using namespace trainz::view;

void Drive::step(const TrackNetwork& net, double seconds) {
    const auto here = net.point(lead);
    const double grade = here ? here->grade : 0;
    const double moved = stepTrain(speed, spec, controls, grade, seconds);
    atBuffer = false;
    if (moved != 0) {
        const auto m = net.advance(lead, moved);
        if (m.stoppedAtEnd) {
            speed = 0;
            atBuffer = true;
        }
    }
}

int addConsist(Scene& scene, const TrackNetwork& net, const TrackPosition& lead, const std::vector<ConsistVehicle>& vehicles,
               const PreparedMesh& bogie, double gap) {
    TrackPosition cursor = lead;
    int placed = 0;
    for (const auto& v : vehicles) {
        if (v.mesh == nullptr || !(v.length > 1)) {
            continue;
        }
        // The two bogie points, one third of the length from the middle.
        TrackPosition front = cursor, back = cursor;
        net.advance(front, -v.length / 6);      // length/2 - length/3
        net.advance(back, -v.length * 5 / 6);   // length/2 + length/3
        const auto a = net.point(front), b = net.point(back);
        if (!a || !b) {
            break;
        }
        // The body between them, its +y toward the front; tilted by their heights.
        const double dx = a->x - b->x, dy = a->y - b->y, flat = std::hypot(dx, dy);
        if (flat < 0.5) {
            break;  // squashed at a buffer: stop placing
        }
        const double heading = std::atan2(dy, dx), yaw = heading - std::numbers::pi / 2;
        const double pitch = std::atan2(a->z - b->z, flat);
        LocalFrame tilt;
        tilt.r[4] = static_cast<float>(std::cos(pitch)), tilt.r[5] = static_cast<float>(std::sin(pitch));
        tilt.r[7] = -tilt.r[5], tilt.r[8] = tilt.r[4];
        addPlacedMesh(scene, *v.mesh, tilt, 0.5 * (a->x + b->x), 0.5 * (a->y + b->y), 0.5 * (a->z + b->z), yaw);
        for (const auto& p : {*a, *b}) {
            addPlacedMesh(scene, bogie, p.x, p.y, p.z, p.heading - std::numbers::pi / 2);
        }
        ++placed;
        net.advance(cursor, -(v.length + gap));
    }
    return placed;
}

Camera chaseCamera(const TrackNetwork& net, const TrackPosition& lead, double back, double up, double side,
                   double ahead) {
    Camera c;
    TrackPosition behind = lead, front = lead;
    net.advance(behind, -back);  // along the track, so it follows curves
    net.advance(front, ahead);
    const auto b = net.point(behind), f = net.point(front);
    if (!b || !f) {
        return c;
    }
    c.target = {f->x, f->y, f->z + 2};
    c.eye = {b->x - std::sin(b->heading) * side, b->y + std::cos(b->heading) * side, b->z + up};
    c.fovYDegrees = 55;
    return c;
}

}  // namespace trainz::game
