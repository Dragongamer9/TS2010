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

bool applyFlyControls(FlyCamera& cam, const FlyControls& c, double seconds, const FlySpeeds& s) {
    auto axis = [](double v) { return std::isfinite(v) ? std::clamp(v, -1.0, 1.0) : 0.0; };
    auto finite = [](double v) { return std::isfinite(v) ? v : 0.0; };
    const double dt = std::isfinite(seconds) ? std::clamp(seconds, 0.0, 0.5) : 0.0;
    const double v = s.metresPerSecond * (c.fast ? s.fastFactor : 1.0) * dt;
    const double fwd = axis(c.forward) * v + finite(c.wheel) * s.metresPerWheelStep * (c.fast ? s.fastFactor : 1.0);
    const double side = axis(c.right) * v, rise = axis(c.up) * v;
    const double yaw = axis(c.turnLeft) * s.turnPerSecond * dt - finite(c.mouseDx) * s.mousePerPixel;
    const double pitch = axis(c.lookUp) * s.lookPerSecond * dt - finite(c.mouseDy) * s.mousePerPixel;
    if (fwd == 0 && side == 0 && rise == 0 && yaw == 0 && pitch == 0) {
        return false;
    }
    cam.move(fwd, side, rise);
    cam.turn(yaw, pitch);
    return true;
}

double stickAxis(int raw, double deadZone) {
    const double v = std::clamp(raw / 32767.0, -1.0, 1.0);
    const double dz = std::isfinite(deadZone) ? std::clamp(deadZone, 0.0, 0.95) : 0.15;
    if (std::fabs(v) <= dz) {
        return 0;
    }
    return (v > 0 ? v - dz : v + dz) / (1 - dz);  // rescaled so the edge of the dead zone is 0
}

}  // namespace trainz::view
