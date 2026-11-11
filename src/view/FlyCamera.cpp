#include "FlyCamera.h"

#include <algorithm>
#include <cmath>

namespace trainz::view {

FlyCamera FlyCamera::lookingAlong(const Camera& c) {
    FlyCamera f;
    f.position = c.eye;
    const double dx = c.target.x - c.eye.x, dy = c.target.y - c.eye.y, dz = c.target.z - c.eye.z;
    const double flat = std::hypot(dx, dy);
    if (flat > 1e-9) {
        f.heading = std::atan2(dy, dx);
    }
    if (flat > 1e-9 || std::fabs(dz) > 1e-9) {
        f.pitch = std::clamp(std::atan2(dz, flat), -kMaxPitch, kMaxPitch);
    }
    return f;
}

Camera FlyCamera::camera(double fovYDegrees) const {
    Camera c;
    c.eye = position;
    const double cp = std::cos(pitch);
    c.target = {position.x + std::cos(heading) * cp * 100, position.y + std::sin(heading) * cp * 100,
                position.z + std::sin(pitch) * 100};
    c.fovYDegrees = fovYDegrees;
    return c;
}

void FlyCamera::move(double forward, double right, double up) {
    if (!std::isfinite(forward) || !std::isfinite(right) || !std::isfinite(up)) {
        return;
    }
    const double ch = std::cos(heading), sh = std::sin(heading);
    position.x += ch * forward + sh * right;  // "right" is clockwise of the heading
    position.y += sh * forward - ch * right;
    position.z += up;
}

void FlyCamera::turn(double dHeading, double dPitch) {
    if (!std::isfinite(dHeading) || !std::isfinite(dPitch)) {
        return;
    }
    heading = std::remainder(heading + dHeading, 2 * 3.14159265358979323846);
    pitch = std::clamp(pitch + dPitch, -kMaxPitch, kMaxPitch);
}

}  // namespace trainz::view
