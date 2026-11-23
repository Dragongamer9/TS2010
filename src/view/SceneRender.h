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

// A small RGBA texture (row by row, top row first), with optional smaller copies.
struct SceneTexture {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    bool alphaTest = false;  // drop texels with alpha < 128 (cut-out leaves, fences); drawn nearest-texel
    bool clampEdges = false;  // true: the edges do not wrap (a baked image covering one area, e.g. a terrain section)
    // Smaller versions, largest first (e.g. the file's own mips). For each triangle the
    // renderer takes the first whose texels are not smaller than the screen pixels, so
    // far surfaces don't shimmer. The top level's alphaTest applies to all of them.
    std::vector<SceneTexture> smaller;
};

struct SceneTriangle {
    ScenePoint p[3];
    Rgb c[3];  // per-vertex colour, interpolated; multiplies the texture when there is one
    int texture = -1;  // index into Scene::textures, or -1
    float u[3] = {0, 0, 0}, v[3] = {0, 0, 0};
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
    std::vector<const SceneTexture*> textures;  // not owned: they must outlive renderScene
    std::vector<SceneTriangle> triangles;
    std::vector<SceneLine> lines;
    std::vector<SceneDot> dots;
};

struct Camera {
    ScenePoint eye;
    ScenePoint target;
    double fovYDegrees = 50;
};

// Bilinear R, G, B (0..255) of a usable texture at (u, v): wrapped into 0..1 (repeating), or
// clamped to it when t.clampEdges; non-finite u or v count as 0. The texture must have
// width x height x 4 bytes (as renderScene checks).
void sampleTextureBilinear(const SceneTexture& t, double u, double v, double out[3]);

// Renders the scene into width x height RGBA (sky gradient behind). Lines and dots get
// a small depth bias so they show on the surface they lie on. Width and height must be
// 1..8192; throws std::invalid_argument otherwise.
std::vector<std::uint8_t> renderScene(const Scene& scene, const Camera& camera, std::uint32_t width,
                                      std::uint32_t height);

}  // namespace trainz::view
