#include "MeshPreview.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace trainz::view {

namespace {

using Vec3 = std::array<float, 3>;

Vec3 project(const trainz::content::MeshVertex& v, PreviewView view) {
    // x right, y up on screen, z towards the viewer
    switch (view) {
        case PreviewView::Side:  // looking along +Y (the long axis of rolling stock)
            return {v.x, v.z, -v.y};
        case PreviewView::Front:  // looking along -X
            return {v.y, v.z, v.x};
        case PreviewView::Top:
            return {v.x, v.y, v.z};
        case PreviewView::ThreeQuarter: {
            const float c = 0.7071f;
            const float rx = c * v.x - c * v.y;
            const float ry = c * v.x + c * v.y;
            // tilt down by about 30 degrees
            return {rx, 0.866f * v.z + 0.5f * ry, -0.5f * v.z + 0.866f * ry};
        }
    }
    return {v.x, v.y, v.z};
}

// Float to int without undefined behaviour for huge or NaN values.
int toInt(float f) {
    if (!(f > -1.0e6f)) {
        return -1000000;
    }
    if (!(f < 1.0e6f)) {
        return 1000000;
    }
    return static_cast<int>(f);
}

}  // namespace

std::vector<std::uint8_t> renderMeshPreview(const trainz::content::MeshFile& mesh, std::uint32_t width,
                                            std::uint32_t height, PreviewView view) {
    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
        throw std::invalid_argument("preview size must be 1..4096");
    }
    std::vector<std::uint8_t> img(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < img.size(); i += 4) {
        img[i] = 40, img[i + 1] = 44, img[i + 2] = 52, img[i + 3] = 255;
    }
    float minX = std::numeric_limits<float>::max(), maxX = -minX, minY = minX, maxY = -minX;
    bool any = false;
    for (const auto& part : mesh.parts) {
        for (const auto& v : part.geometry.vertices) {
            const Vec3 p = project(v, view);
            if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) {
                continue;
            }
            minX = std::min(minX, p[0]), maxX = std::max(maxX, p[0]);
            minY = std::min(minY, p[1]), maxY = std::max(maxY, p[1]);
            any = true;
        }
    }
    if (!any) {
        return img;
    }
    float spanX = std::max(maxX - minX, 1e-6f);
    float spanY = std::max(maxY - minY, 1e-6f);
    if (!std::isfinite(spanX) || !std::isfinite(spanY)) {  // finite coordinates whose difference overflows
        spanX = std::numeric_limits<float>::max();
        spanY = std::numeric_limits<float>::max();
    }
    const float scale = 0.9f * std::min(static_cast<float>(width) / spanX, static_cast<float>(height) / spanY);
    const float cx = minX / 2 + maxX / 2, cy = minY / 2 + maxY / 2;  // halves first: no overflow
    std::vector<float> depth(static_cast<std::size_t>(width) * height, -std::numeric_limits<float>::max());
    const Vec3 light = {0.4f, 0.3f, 0.86f};

    for (const auto& part : mesh.parts) {
        const auto& g = part.geometry;
        for (std::size_t t = 0; t + 2 < g.indices.size(); t += 3) {
            Vec3 p[3] = {};
            bool ok = true;
            for (int k = 0; k < 3; ++k) {
                const std::size_t vi = g.indices[t + static_cast<std::size_t>(k)];
                if (vi >= g.vertices.size()) {  // only possible for hand-built meshes; parseMesh checks this
                    ok = false;
                    break;
                }
                p[k] = project(g.vertices[vi], view);
                ok = ok && std::isfinite(p[k][0]) && std::isfinite(p[k][1]) && std::isfinite(p[k][2]);
            }
            if (!ok) {
                continue;
            }
            const Vec3 u = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
            const Vec3 w = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
            const Vec3 n = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (!(len > 0) || !std::isfinite(len)) {  // degenerate, or so large the maths overflowed
                continue;
            }
            const float shade = std::fabs(n[0] * light[0] + n[1] * light[1] + n[2] * light[2]) / len;
            const auto col = static_cast<std::uint8_t>(60 + 180 * std::min(1.0f, shade));
            float sx[3] = {}, sy[3] = {};
            for (int k = 0; k < 3; ++k) {
                sx[k] = (p[k][0] - cx) * scale + static_cast<float>(width) / 2;
                sy[k] = static_cast<float>(height) / 2 - (p[k][1] - cy) * scale;
            }
            bool screenOk = true;
            for (int k = 0; k < 3; ++k) {
                screenOk = screenOk && std::isfinite(sx[k]) && std::isfinite(sy[k]);
            }
            const float den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2]);
            if (!screenOk || !std::isfinite(den) || std::fabs(den) < 1e-9f) {
                continue;
            }
            const int x0 = std::max(0, toInt(std::floor(std::min({sx[0], sx[1], sx[2]}))));
            const int x1 = std::min(static_cast<int>(width) - 1, toInt(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
            const int y0 = std::max(0, toInt(std::floor(std::min({sy[0], sy[1], sy[2]}))));
            const int y1 = std::min(static_cast<int>(height) - 1, toInt(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float fx = static_cast<float>(x) + 0.5f, fy = static_cast<float>(y) + 0.5f;
                    const float w0 = ((sy[1] - sy[2]) * (fx - sx[2]) + (sx[2] - sx[1]) * (fy - sy[2])) / den;
                    const float w1 = ((sy[2] - sy[0]) * (fx - sx[2]) + (sx[0] - sx[2]) * (fy - sy[2])) / den;
                    const float w2 = 1 - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) {
                        continue;
                    }
                    const float z = w0 * p[0][2] + w1 * p[1][2] + w2 * p[2][2];
                    const std::size_t k = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
                    if (z > depth[k]) {
                        depth[k] = z;
                        img[4 * k] = col, img[4 * k + 1] = col, img[4 * k + 2] = static_cast<std::uint8_t>(col * 0.95f);
                        img[4 * k + 3] = 255;
                    }
                }
            }
        }
    }
    return img;
}

std::size_t poseMesh(trainz::content::MeshFile& mesh, const trainz::content::AnimationFile& anim, std::size_t frame) {
    if (frame >= anim.frames.size()) {
        throw std::out_of_range("animation frame " + std::to_string(frame) + " does not exist");
    }
    // Check everything before touching the mesh (AnimationFile can be built by hand).
    const auto& poses = anim.frames[frame];
    if (poses.size() < anim.bones.size()) {
        throw std::out_of_range("animation frame " + std::to_string(frame) + " has " + std::to_string(poses.size()) +
                                " poses for " + std::to_string(anim.bones.size()) + " bones");
    }
    std::size_t moved = 0;
    for (auto& part : mesh.parts) {
        auto& g = part.geometry;
        if (g.boneName.empty()) {
            continue;
        }
        const int b = anim.boneIndex(g.boneName);
        if (b < 0) {
            continue;
        }
        const auto& pose = poses[static_cast<std::size_t>(b)];
        for (auto& v : g.vertices) {
            const auto p = trainz::content::rotate(pose.rotation, {v.x, v.y, v.z});
            v.x = p[0] + pose.position[0];
            v.y = p[1] + pose.position[1];
            v.z = p[2] + pose.position[2];
        }
        for (auto& n : g.vertexNormals) {
            n = trainz::content::rotate(pose.rotation, n);
        }
        for (auto& n : g.faceNormals) {
            n = trainz::content::rotate(pose.rotation, n);
        }
        ++moved;
    }
    return moved;
}

}  // namespace trainz::view
