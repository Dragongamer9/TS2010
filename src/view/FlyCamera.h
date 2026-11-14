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

// One frame of fly controls, gathered from keys, mouse and a gamepad. Axes are -1..1
// (values outside are clamped; a gamepad's small values within `deadZone` count as 0).
struct FlyControls {
    double forward = 0, right = 0, up = 0;  // movement
    double turnLeft = 0, lookUp = 0;        // turning, from keys or a stick
    double mouseDx = 0, mouseDy = 0;        // pixels dragged this frame (right = +x, down = +y)
    double wheel = 0;                       // wheel steps (forward = +)
    bool fast = false;
};

struct FlySpeeds {
    double metresPerSecond = 30, fastFactor = 5;
    double turnPerSecond = 1.2, lookPerSecond = 0.8;  // radians
    double mousePerPixel = 0.004;                     // radians
    double metresPerWheelStep = 10;
};

// Applies the controls for `seconds` (clamped to 0..0.5). Returns whether anything moved.
bool applyFlyControls(FlyCamera& cam, const FlyControls& c, double seconds, const FlySpeeds& s = {});

// A gamepad stick value (-32768..32767) as -1..1, with a dead zone of `deadZone` (0..1) where it counts as 0.
double stickAxis(int raw, double deadZone = 0.15);

}  // namespace trainz::view
