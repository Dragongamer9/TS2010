#include "SceneRender.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace trainz::view {

namespace {

struct V3 {
    double x, y, z;
};

V3 sub(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 normalize(const V3& a) {
    const double l = std::sqrt(dot(a, a));
    return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : V3{0, 0, 0};
}
V3 v3(const ScenePoint& p) { return {p.x, p.y, p.z}; }

// Camera-space projection: screen x/y in pixels and the distance along the view axis.
struct Projected {
    double sx = 0, sy = 0, depth = 0;
    bool visible = false;
};

class Projector {
public:
    Projector(const Camera& cam, std::uint32_t w, std::uint32_t h) : w_(w), h_(h) {
        eye_ = v3(cam.eye);
        fwd_ = normalize(sub(v3(cam.target), eye_));
        right_ = normalize(cross(fwd_, V3{0, 0, 1}));
        if (dot(right_, right_) == 0) {
            right_ = {1, 0, 0};  // looking straight down
        }
        up_ = cross(right_, fwd_);
        f_ = (h / 2.0) / std::tan(std::clamp(cam.fovYDegrees, 5.0, 150.0) * std::numbers::pi / 360.0);
    }
    Projected operator()(const ScenePoint& p) const {
        const V3 d = sub(v3(p), eye_);
        Projected out;
        out.depth = dot(d, fwd_);
        if (!(out.depth > kNear) || !std::isfinite(out.depth)) {
            return out;
        }
        out.sx = w_ / 2.0 + f_ * dot(d, right_) / out.depth;
        out.sy = h_ / 2.0 - f_ * dot(d, up_) / out.depth;
        out.visible = std::isfinite(out.sx) && std::isfinite(out.sy);
        return out;
    }
    static constexpr double kNear = 1.0;

private:
    std::uint32_t w_, h_;
    V3 eye_{}, fwd_{}, right_{}, up_{};
    double f_ = 1;
};

bool usable(const SceneTexture& t) {
    return t.width > 0 && t.height > 0 && t.rgba.size() >= static_cast<std::size_t>(t.width) * t.height * 4;
}

std::uint8_t clamp8(double v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0)); }

}  // namespace

std::vector<std::uint8_t> renderScene(const Scene& scene, const Camera& camera, std::uint32_t width,
                                      std::uint32_t height) {
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        throw std::invalid_argument("image size must be 1..8192 per side");
    }
    const std::size_t n = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> rgba(n * 4);
    for (std::uint32_t y = 0; y < height; ++y) {  // sky
        const double t = static_cast<double>(y) / height;
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t* p = rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            p[0] = clamp8(120 + 80 * t), p[1] = clamp8(165 + 60 * t), p[2] = clamp8(220 + 25 * t), p[3] = 255;
        }
    }
    std::vector<double> depth(n, std::numeric_limits<double>::infinity());
    const Projector proj(camera, width, height);
    const V3 sun = normalize(V3{-0.4, -0.6, 0.7});

    auto put = [&](int x, int y, double z, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        if (x < 0 || y < 0 || x >= static_cast<int>(width) || y >= static_cast<int>(height)) {
            return;
        }
        const std::size_t k = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
        if (z < depth[k]) {
            depth[k] = z;
            std::uint8_t* p = rgba.data() + k * 4;
            p[0] = r, p[1] = g, p[2] = b, p[3] = 255;
        }
    };

    for (const auto& t : scene.triangles) {
        Projected q[3];
        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            q[i] = proj(t.p[i]);
            ok = ok && q[i].visible;
        }
        if (!ok) {
            continue;  // behind the camera (no clipping: the far terrain is what matters here)
        }
        const SceneTexture* tex = nullptr;
        if (t.texture >= 0 && static_cast<std::size_t>(t.texture) < scene.textures.size()) {
            tex = scene.textures[static_cast<std::size_t>(t.texture)];
            if (tex != nullptr && !usable(*tex)) {
                tex = nullptr;  // malformed: draw untextured
            }
        }
        const V3 nrm = normalize(cross(sub(v3(t.p[1]), v3(t.p[0])), sub(v3(t.p[2]), v3(t.p[0]))));
        const double light = 0.45 + 0.55 * std::max(0.0, std::fabs(dot(nrm, sun)));
        const double minX = std::floor(std::min({q[0].sx, q[1].sx, q[2].sx}));
        const double maxX = std::ceil(std::max({q[0].sx, q[1].sx, q[2].sx}));
        const double minY = std::floor(std::min({q[0].sy, q[1].sy, q[2].sy}));
        const double maxY = std::ceil(std::max({q[0].sy, q[1].sy, q[2].sy}));
        if (maxX < 0 || maxY < 0 || minX >= width || minY >= height || maxX - minX > 4.0 * width ||
            maxY - minY > 4.0 * height) {
            continue;
        }
        const double area = (q[1].sx - q[0].sx) * (q[2].sy - q[0].sy) - (q[2].sx - q[0].sx) * (q[1].sy - q[0].sy);
        if (std::fabs(area) < 1e-9) {
            continue;
        }
        const bool alphaTest = tex != nullptr && tex->alphaTest;  // the top level's choice, for every level
        if (tex != nullptr && !tex->smaller.empty()) {
            // Texels per screen pixel, from the triangle's texture area and screen area.
            const double du1 = t.u[1] - t.u[0], dv1 = t.v[1] - t.v[0], du2 = t.u[2] - t.u[0], dv2 = t.v[2] - t.v[0];
            const double uvArea = std::fabs(du1 * dv2 - du2 * dv1);
            double ratio = uvArea * tex->width * tex->height / std::fabs(area);
            for (const auto& m : tex->smaller) {
                if (!(ratio > 2.0) || !usable(m)) {  // within a factor of 2: keep this level
                    break;
                }
                ratio *= static_cast<double>(m.width) * m.height / (static_cast<double>(tex->width) * tex->height);
                tex = &m;
            }
        }
        const int x0 = std::max(0, static_cast<int>(minX)), x1 = std::min(static_cast<int>(width) - 1, static_cast<int>(maxX));
        const int y0 = std::max(0, static_cast<int>(minY)), y1 = std::min(static_cast<int>(height) - 1, static_cast<int>(maxY));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                const double w0 = ((q[1].sx - px) * (q[2].sy - py) - (q[2].sx - px) * (q[1].sy - py)) / area;
                const double w1 = ((q[2].sx - px) * (q[0].sy - py) - (q[0].sx - px) * (q[2].sy - py)) / area;
                const double w2 = 1.0 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) {
                    continue;
                }
                const double z = w0 * q[0].depth + w1 * q[1].depth + w2 * q[2].depth;
                double r = w0 * t.c[0].r + w1 * t.c[1].r + w2 * t.c[2].r;
                double gg = w0 * t.c[0].g + w1 * t.c[1].g + w2 * t.c[2].g;
                double b = w0 * t.c[0].b + w1 * t.c[1].b + w2 * t.c[2].b;
                if (tex != nullptr) {
                    // Perspective-correct: interpolate u/z, v/z and 1/z across the screen.
                    const double iz = w0 / q[0].depth + w1 / q[1].depth + w2 / q[2].depth;
                    const double uu = (w0 * t.u[0] / q[0].depth + w1 * t.u[1] / q[1].depth + w2 * t.u[2] / q[2].depth) / iz;
                    const double vv = (w0 * t.v[0] / q[0].depth + w1 * t.v[1] / q[1].depth + w2 * t.v[2] / q[2].depth) / iz;
                    const double fu = uu - std::floor(uu), fv = vv - std::floor(vv);  // wrap
                    const auto tx = std::min<std::uint32_t>(tex->width - 1, static_cast<std::uint32_t>(fu * tex->width));
                    const auto ty = std::min<std::uint32_t>(tex->height - 1, static_cast<std::uint32_t>(fv * tex->height));
                    const std::uint8_t* texel = tex->rgba.data() + (static_cast<std::size_t>(ty) * tex->width + tx) * 4;
                    if (alphaTest && texel[3] < 128) {
                        continue;
                    }
                    r = r * texel[0] / 255.0, gg = gg * texel[1] / 255.0, b = b * texel[2] / 255.0;
                }
                put(x, y, z, clamp8(light * r), clamp8(light * gg), clamp8(light * b));
            }
        }
    }
    for (const auto& l : scene.lines) {
        const Projected a = proj(l.a), b = proj(l.b);
        if (!a.visible || !b.visible) {
            continue;
        }
        const double len = std::hypot(b.sx - a.sx, b.sy - a.sy);
        if (len > 4.0 * (width + height)) {
            continue;
        }
        const int steps = static_cast<int>(std::ceil(len)) + 1;
        for (int k = 0; k <= steps; ++k) {
            const double f = static_cast<double>(k) / steps;
            const double z = (a.depth + (b.depth - a.depth) * f) * 0.995;  // bias toward the camera
            put(static_cast<int>(std::floor(a.sx + (b.sx - a.sx) * f)), static_cast<int>(std::floor(a.sy + (b.sy - a.sy) * f)), z,
                l.c.r, l.c.g, l.c.b);
        }
    }
    for (const auto& d : scene.dots) {
        const Projected p = proj(d.p);
        if (!p.visible) {
            continue;
        }
        const int r = std::clamp(d.radius, 0, 8);
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                put(static_cast<int>(std::floor(p.sx)) + dx, static_cast<int>(std::floor(p.sy)) + dy, p.depth * 0.99, d.c.r,
                    d.c.g, d.c.b);
            }
        }
    }
    return rgba;
}

}  // namespace trainz::view
