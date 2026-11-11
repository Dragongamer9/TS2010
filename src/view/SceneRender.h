// A small perspective software renderer for looking at decoded routes in 3D: coloured
// triangles with simple sun shading, lines and points, all depth-tested. No GPU, no
// libraries. Not the game renderer: it draws only what the port has decoded.
// World axes as in the route files: +x east, +y north, +z up, metres.
#pragma once

#include <cstdint>
#include <vector>

namespace trainz::view {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
};

struct ScenePoint {
    double x = 0, y = 0, z = 0;
};

struct SceneTriangle {
    ScenePoint p[3];
    Rgb c[3];  // per-vertex colour, interpolated
};

struct SceneLine {
    ScenePoint a, b;
    Rgb c;
};

struct SceneDot {
    ScenePoint p;
    Rgb c;
    int radius = 1;  // pixels
};

struct Scene {
    std::vector<SceneTriangle> triangles;
    std::vector<SceneLine> lines;
    std::vector<SceneDot> dots;
};

struct Camera {
    ScenePoint eye;
    ScenePoint target;
    double fovYDegrees = 50;
};

// Renders the scene into width x height RGBA (sky gradient behind). Lines and dots get
// a small depth bias so they show on the surface they lie on. Width and height must be
// 1..8192; throws std::invalid_argument otherwise.
std::vector<std::uint8_t> renderScene(const Scene& scene, const Camera& camera, std::uint32_t width,
                                      std::uint32_t height);

}  // namespace trainz::view
