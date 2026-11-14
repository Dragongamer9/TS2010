#include "GeneratedWorld.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace trainz::game {

using namespace trainz::content;
using namespace trainz::view;

namespace {

constexpr double kStraight = 600, kRadius = 150;
constexpr std::uint8_t kStraightFlag = 0x20;

void addVertex(TrackLayout& t, std::int32_t index, double x, double y, double z) {
    TrackVertex v;
    v.index = index;
    // Baseboards are 720 m: keep the board numbers right for negative positions too.
    v.boardX = static_cast<std::int16_t>(std::floor(x / kBaseboardSize));
    v.boardY = static_cast<std::int16_t>(std::floor(y / kBaseboardSize));
    v.x = static_cast<float>(x - v.boardX * kBaseboardSize);
    v.y = static_cast<float>(y - v.boardY * kBaseboardSize);
    v.z = static_cast<float>(z);
    t.vertices.emplace(index, v);
}

void addPiece(TrackLayout& t, std::int32_t index, std::int32_t from, std::int32_t to, bool straight) {
    TrackStretch s;
    s.index = index, s.from = from, s.to = to;
    s.rest = {static_cast<std::uint8_t>(straight ? kStraightFlag : 0), 0, 0, 0};
    t.stretchAt.emplace(index, t.stretches.size());
    t.stretches.push_back(s);
    // The piece leaves `from` on side 1 and arrives at `to` on side 0 (vertex sides).
    t.vertices.at(from).links.push_back({index, 1});
    t.vertices.at(to).links.push_back({index, 0});
}

double hills(double x, double y, std::uint32_t seed) {
    const double a = 0.7 * (seed % 97), b = 1.3 * (seed % 89);
    return 9 * std::sin(x / 170 + a) * std::cos(y / 130 + b) + 5 * std::sin((x + y) / 90 + a * 0.5) +
           3 * std::cos((x - 2 * y) / 60 + b);
}

}  // namespace

double GeneratedWorld::heightAt(double x, double y) const {
    const double fx = (x - x0) / cellSize, fy = (y - y0) / cellSize;
    if (!(fx >= 0 && fy >= 0 && fx <= cellsX && fy <= cellsY)) {
        return 0;
    }
    const int ix = std::min(cellsX - 1, static_cast<int>(fx)), iy = std::min(cellsY - 1, static_cast<int>(fy));
    const double tx = fx - ix, ty = fy - iy;
    auto h = [&](int cx, int cy) { return static_cast<double>(heights[static_cast<std::size_t>(cy) * (cellsX + 1) + cx]); };
    return (1 - ty) * ((1 - tx) * h(ix, iy) + tx * h(ix + 1, iy)) + ty * ((1 - tx) * h(ix, iy + 1) + tx * h(ix + 1, iy + 1));
}

GeneratedWorld generateWorld(std::uint32_t seed) {
    GeneratedWorld w;
    TrackLayout& t = w.track;
    // Main line, anticlockwise: south straight (split at the turnout), east bend, north
    // straight, west bend. Track level at z = 0.
    std::vector<std::pair<double, double>> ring;
    const double hx = kStraight / 2;
    ring.push_back({-hx, -kRadius});
    ring.push_back({-100, -kRadius});  // the turnout
    ring.push_back({hx, -kRadius});
    for (int k = 1; k < 12; ++k) {  // east bend, every 15 degrees
        const double a = -std::numbers::pi / 2 + k * std::numbers::pi / 12;
        ring.push_back({hx + kRadius * std::cos(a), kRadius * std::sin(a)});
    }
    ring.push_back({hx, kRadius});
    ring.push_back({-hx, kRadius});
    for (int k = 1; k < 12; ++k) {  // west bend
        const double a = std::numbers::pi / 2 + k * std::numbers::pi / 12;
        ring.push_back({-hx + kRadius * std::cos(a), kRadius * std::sin(a)});
    }
    const auto n = static_cast<std::int32_t>(ring.size());
    for (std::int32_t i = 0; i < n; ++i) {
        addVertex(t, i, ring[static_cast<std::size_t>(i)].first, ring[static_cast<std::size_t>(i)].second, 0);
    }
    for (std::int32_t i = 0; i < n; ++i) {
        const auto& a = ring[static_cast<std::size_t>(i)];
        const auto& b = ring[static_cast<std::size_t>((i + 1) % n)];
        const bool straight = std::fabs(a.second - b.second) < 1e-6;  // the two straights
        addPiece(t, 100 + i, i, (i + 1) % n, straight);
    }
    w.junctionVertex = 1;
    w.startPiece = 100;
    // Siding: from the turnout, an S-curve 12 m to the south, then 150 m straight to a buffer.
    addVertex(t, 1000, 0, -kRadius - 12, 0);
    addVertex(t, 1001, 150, -kRadius - 12, 0);
    addPiece(t, 2000, 1, 1000, false);  // leaves the turnout on side 1, with the main line on
    addPiece(t, 2001, 1000, 1001, true);
    w.sidingPiece = 2000;

    // Terrain: hills, flattened smoothly to the track's level within 25 m of it.
    const TrackLocator near(t, 0, 0, 1e6);
    w.x0 = -560, w.y0 = -360, w.cellSize = 10, w.cellsX = 112, w.cellsY = 72;
    w.heights.resize(static_cast<std::size_t>(w.cellsX + 1) * (w.cellsY + 1));
    for (int cy = 0; cy <= w.cellsY; ++cy) {
        for (int cx = 0; cx <= w.cellsX; ++cx) {
            const double x = w.x0 + cx * w.cellSize, y = w.y0 + cy * w.cellSize;
            const auto s = near.nearest(x, y, 25.0);
            const double d = s ? s->distance : 25.0;
            const double k = std::clamp((d - 6) / 19, 0.0, 1.0);
            const double blend = k * k * (3 - 2 * k);  // smoothstep
            w.heights[static_cast<std::size_t>(cy) * (w.cellsX + 1) + cx] = static_cast<float>(-0.4 + blend * hills(x, y, seed));
        }
    }
    return w;
}

void addTerrain(Scene& scene, const GeneratedWorld& w) {
    const TrackLocator near(w.track, 0, 0, 1e6);
    auto point = [&](int cx, int cy) {
        const double x = w.x0 + cx * w.cellSize, y = w.y0 + cy * w.cellSize;
        return ScenePoint{x, y, w.heights[static_cast<std::size_t>(cy) * (w.cellsX + 1) + cx]};
    };
    auto colour = [&](const ScenePoint& p) {
        const auto s = near.nearest(p.x, p.y, 5.0);
        if (s && s->distance < 4) {
            return Rgb{120, 115, 105};  // ballast
        }
        const double k = std::clamp((p.z + 12) / 30, 0.0, 1.0);
        return Rgb{static_cast<std::uint8_t>(70 + 40 * k), static_cast<std::uint8_t>(120 + 30 * k), static_cast<std::uint8_t>(55 + 20 * k)};
    };
    for (int cy = 0; cy < w.cellsY; ++cy) {
        for (int cx = 0; cx < w.cellsX; ++cx) {
            const ScenePoint a = point(cx, cy), b = point(cx + 1, cy), c = point(cx, cy + 1), d = point(cx + 1, cy + 1);
            scene.triangles.push_back({{a, b, d}, {colour(a), colour(b), colour(d)}});
            scene.triangles.push_back({{a, d, c}, {colour(a), colour(d), colour(c)}});
        }
    }
}

void addTrack(Scene& scene, const GeneratedWorld& w) {
    constexpr double kGauge = 1.435;
    for (const auto& st : w.track.stretches) {
        const auto c = trackCurve(w.track, st);
        if (!c) {
            continue;
        }
        const double len = std::hypot(c->p1[0] - c->p0[0], c->p1[1] - c->p0[1]);
        const int pieces = std::clamp(static_cast<int>(std::ceil(len / 4)), 1, 400);
        ScenePoint prev[2];
        for (int k = 0; k <= pieces; ++k) {
            const double s = static_cast<double>(k) / pieces, e = 1e-3;
            double q[3], q2[3];
            c->point(s, q);
            c->point(std::min(1.0, s + e), q2);
            if (s + e > 1) {
                c->point(s - e, q2), q2[0] = 2 * q[0] - q2[0], q2[1] = 2 * q[1] - q2[1];
            }
            const double hx = q2[0] - q[0], hy = q2[1] - q[1], hl = std::hypot(hx, hy);
            const double nx = hl > 0 ? -hy / hl : 0, ny = hl > 0 ? hx / hl : 0;
            for (int r = 0; r < 2; ++r) {
                const double side = (r == 0 ? -0.5 : 0.5) * kGauge;
                const ScenePoint p{q[0] + nx * side, q[1] + ny * side, q[2] + 0.2};
                if (k > 0) {
                    scene.lines.push_back({prev[r], p, Rgb{90, 80, 70}});
                }
                prev[r] = p;
            }
        }
    }
}

void addBox(PreparedMesh& m, const float lo[3], const float hi[3], Rgb colour) {
    const float v[8][3] = {{lo[0], lo[1], lo[2]}, {hi[0], lo[1], lo[2]}, {hi[0], hi[1], lo[2]}, {lo[0], hi[1], lo[2]},
                           {lo[0], lo[1], hi[2]}, {hi[0], lo[1], hi[2]}, {hi[0], hi[1], hi[2]}, {lo[0], hi[1], hi[2]}};
    static constexpr int f[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                                     {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    for (const auto& tri : f) {
        LocalTriangle t{};
        for (int k = 0; k < 3; ++k) {
            std::copy(v[tri[k]], v[tri[k]] + 3, t.p[k]);
        }
        t.colour = colour;
        m.triangles.push_back(t);
    }
}

namespace {

PreparedMesh vehicle(float length, float width, float height, Rgb body, const char* name) {
    PreparedMesh m;
    m.meshName = name;
    const float hl = length / 2, hw = width / 2;
    const float lo[3] = {-hw, -hl, 1.1F}, hi[3] = {hw, hl, 1.1F + height};
    addBox(m, lo, hi, body);
    const float flo[3] = {-hw + 0.1F, -hl + 0.3F, 0.9F}, fhi[3] = {hw - 0.1F, hl - 0.3F, 1.1F};  // frame
    addBox(m, flo, fhi, Rgb{40, 40, 45});
    LocalFrame b0, b1;  // bogie points, one third of the length from the middle
    b0.t[1] = -length / 3, b1.t[1] = length / 3;
    m.attachments.emplace("a.bog0", b0);
    m.attachments.emplace("a.bog1", b1);
    return m;
}

}  // namespace

PreparedMesh boxLocomotive() {
    PreparedMesh m = vehicle(18, 3, 2.8F, Rgb{200, 60, 40}, "generated locomotive");
    const float lo[3] = {-1.4F, 5.5F, 3.9F}, hi[3] = {1.4F, 8.6F, 4.6F};  // cab roof at the +y end
    addBox(m, lo, hi, Rgb{60, 60, 70});
    return m;
}

PreparedMesh boxWagon(Rgb colour) { return vehicle(14, 2.9F, 2.4F, colour, "generated wagon"); }

PreparedMesh boxBogie() {
    PreparedMesh m;
    m.meshName = "generated bogie";
    const float lo[3] = {-1.2F, -1.4F, 0.2F}, hi[3] = {1.2F, 1.4F, 0.9F};
    addBox(m, lo, hi, Rgb{30, 30, 30});
    return m;
}

}  // namespace trainz::game
