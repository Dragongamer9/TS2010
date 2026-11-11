// A free-flying camera for the interactive route viewer: a position, a heading (radians,
// anticlockwise from +x, as object yaw) and a pitch (radians, positive looking up). Moving
// forward and sideways stays level (like walking); up and down move along +z.
#pragma once

#include "view/SceneRender.h"

namespace trainz::view {

class FlyCamera {
public:
    // Takes over a camera's eye and direction (e.g. route-tool's default view).
    static FlyCamera lookingAlong(const Camera& c);

    Camera camera(double fovYDegrees = 60) const;
    void move(double forward, double right, double up);  // metres
    void turn(double heading, double pitch);              // radians; pitch is kept within +-85 degrees

    ScenePoint position;
    double heading = 0;
    double pitch = 0;

    static constexpr double kMaxPitch = 85.0 * 3.14159265358979323846 / 180.0;
};

}  // namespace trainz::view
