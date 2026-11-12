#include "Drive.h"

#include "view/AssetMeshes.h"

#include <cmath>
#include <numbers>

namespace trainz::game {

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
