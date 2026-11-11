#include "RouteScene.h"

#include "content/ContentError.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <optional>

namespace trainz::view {

using namespace trainz::content;

namespace {

double worldOf(std::int16_t board, float local) { return board * kBaseboardSize + local; }

bool fits(const Scene& scene, const PreparedMesh& mesh) { return sceneHasRoomFor(scene, mesh.triangles.size()); }

}  // namespace

double headingAlong(double h, double ref) {
    const double d = std::remainder(h - ref, 2 * std::numbers::pi);
    return std::fabs(d) <= std::numbers::pi / 2 ? ref + d : ref + std::remainder(d + std::numbers::pi, 2 * std::numbers::pi);
}

bool sceneHasRoomFor(const Scene& scene, std::size_t triangles) {
    return scene.triangles.size() <= kMaxSceneTriangles && triangles <= kMaxSceneTriangles - scene.triangles.size();
}

RouteView buildRouteView(const RouteSceneInput& in, double radius, std::optional<double> centreX,
                         std::optional<double> centreY) {
    if (in.ground == nullptr || in.ground->sections.empty()) {
        throw ContentError("route has no terrain sections");
    }
    const GroundFile& g = *in.ground;
    RouteView v;

    // Pick the centre.
    if (centreX && centreY) {
        v.centreX = *centreX, v.centreY = *centreY;
    } else if (in.trains != nullptr && !in.trains->empty() && !in.trains->front().vehicles.empty()) {
        const auto& veh = in.trains->front().vehicles.front();
        v.centreX = worldOf(veh.boardX, veh.x), v.centreY = worldOf(veh.boardY, veh.y);
    } else if (in.track != nullptr) {
        std::vector<std::pair<double, double>> j;
        for (const auto& [i, tv] : in.track->vertices) {
            if (tv.links.size() >= 3) {
                j.emplace_back(tv.worldX(), tv.worldY());
            }
        }
        std::size_t best = 0;
        for (const auto& a : j) {
            std::size_t n = 0;
            for (const auto& b : j) {
                n += std::hypot(a.first - b.first, a.second - b.second) < 300.0;
            }
            if (n > best) {
                best = n, v.centreX = a.first, v.centreY = a.second;
            }
        }
        if (best == 0) {
            v.centreX = (g.sections.front().x + 0.5) * kBaseboardSize, v.centreY = (g.sections.front().y + 0.5) * kBaseboardSize;
        }
    } else {
        v.centreX = (g.sections.front().x + 0.5) * kBaseboardSize, v.centreY = (g.sections.front().y + 0.5) * kBaseboardSize;
    }
    auto near = [&](double x, double y) { return std::hypot(x - v.centreX, y - v.centreY) <= radius; };

    // Terrain: two triangles per cell, coloured from the section's 64x64 overview map.
    double centreZ = 0;
    bool haveCentreZ = false;
    for (const auto& s : g.sections) {
        const double ox = s.x * kBaseboardSize, oy = s.y * kBaseboardSize;
        if (v.centreX < ox - radius || v.centreX > ox + kBaseboardSize + radius || v.centreY < oy - radius ||
            v.centreY > oy + kBaseboardSize + radius) {
            continue;
        }
        GroundSectionData d;
        try {
            d = parseGroundSection(s, g.version);
        } catch (const ContentError&) {
            continue;
        }
        const std::size_t side = groundVertexSide(s.tag);
        if (side < 8 || d.vertices.size() != side * side) {
            continue;
        }
        ++v.sections;
        const std::size_t cells = side - 4;
        const double step = kBaseboardSize / static_cast<double>(cells);
        auto colour = [&](std::size_t row, std::size_t col) {
            Rgb c{110, 130, 90};
            if (s.overview.size() == 64 * 64) {
                const std::size_t oc = std::min<std::size_t>(63, col * 64 / cells);
                const std::size_t orow = 63 - std::min<std::size_t>(63, row * 64 / cells);  // overview row 0 is the +y edge
                overviewToRgb(s.overview[orow * 64 + oc], c.r, c.g, c.b);
            }
            return c;
        };
        auto point = [&](std::size_t row, std::size_t col) {
            return ScenePoint{ox + col * step, oy + row * step, d.vertices[row * side + col].height};
        };
        for (std::size_t r = 0; r < cells; ++r) {
            for (std::size_t c = 0; c < cells; ++c) {
                const ScenePoint p00 = point(r, c), p01 = point(r, c + 1), p10 = point(r + 1, c), p11 = point(r + 1, c + 1);
                if (!near(p00.x, p00.y) && !near(p11.x, p11.y)) {
                    continue;
                }
                if (!std::isfinite(p00.z) || !std::isfinite(p01.z) || !std::isfinite(p10.z) || !std::isfinite(p11.z)) {
                    continue;
                }
                const Rgb c00 = colour(r, c), c01 = colour(r, c + 1), c10 = colour(r + 1, c), c11 = colour(r + 1, c + 1);
                v.scene.triangles.push_back({{p00, p01, p11}, {c00, c01, c11}});
                v.scene.triangles.push_back({{p00, p11, p10}, {c00, c11, c10}});
                if (!haveCentreZ && std::fabs(p00.x - v.centreX) < step && std::fabs(p00.y - v.centreY) < step) {
                    centreZ = p00.z, haveCentreZ = true;
                }
            }
        }
    }

    if (in.objects != nullptr) {
        for (const auto& o : *in.objects) {
            const double x = o.worldX(), y = o.worldY();
            if (near(x, y) && std::isfinite(o.z)) {
                if (in.meshes != nullptr) {
                    if (const PreparedMesh* m = in.meshes->get(o.kuid); m != nullptr && fits(v.scene, *m)) {
                        // Yaw: radians, anticlockwise (inferred); which side faces +x at 0 is not known.
                        addPlacedMesh(v.scene, *m, x, y, o.z, o.yaw);
                        ++v.objectsAsMeshes;
                        continue;
                    }
                }
                const Rgb c = o.tag == "sgOb" ? Rgb{230, 40, 230} : Rgb{25, 85, 35};
                v.scene.dots.push_back({{x, y, o.z + 1.0}, c, o.tag == "sgOb" ? 2 : 1});
                ++v.objectsAsDots;
            }
        }
    }
    if (in.track != nullptr) {
        for (const auto& st : in.track->stretches) {
            const auto a = in.track->vertices.find(st.from), b = in.track->vertices.find(st.to);
            if (a == in.track->vertices.end() || b == in.track->vertices.end()) {
                continue;
            }
            const ScenePoint pa{a->second.worldX(), a->second.worldY(), a->second.z + 0.5};
            const ScenePoint pb{b->second.worldX(), b->second.worldY(), b->second.z + 0.5};
            if (!near(pa.x, pa.y) || !near(pb.x, pb.y)) {  // both ends inside: nothing sticks out past the terrain
                continue;
            }
            const Rgb colour = st.kind == "tkBr" ? Rgb{240, 210, 40} : Rgb{200, 30, 30};
            const auto curve = trackCurve(*in.track, st);
            if (!curve || st.straight()) {
                v.scene.lines.push_back({pa, pb, colour});
                continue;
            }
            // The port's curve model (TrackLayout.h), in pieces of about 5 m (2..64).
            const double len = std::hypot(pb.x - pa.x, pb.y - pa.y);
            const int pieces = std::clamp(static_cast<int>(std::ceil(len / 5.0)), 2, 64);
            ScenePoint prev = pa;
            for (int k = 1; k <= pieces; ++k) {
                double q[3];
                curve->point(static_cast<double>(k) / pieces, q);
                const ScenePoint p{q[0], q[1], q[2] + 0.5};
                v.scene.lines.push_back({prev, p, colour});
                prev = p;
            }
            ++v.curvedPieces;
        }
    }
    if (in.trains != nullptr) {
        // PREVIEW APPROXIMATION, not decoded data: a vehicle's stored z is often 0.3 (its
        // meaning is not known), so the train is drawn at the height of its own track piece.
        std::map<std::int32_t, const TrackStretch*> pieces;
        if (in.track != nullptr) {
            for (const auto& st : in.track->stretches) {
                pieces.emplace(st.index, &st);
            }
        }
        // Vehicles and bogies follow the modelled track (TrackLayout.h) where it is near.
        std::optional<TrackLocator> onTrack;
        if (in.track != nullptr) {
            onTrack.emplace(*in.track, v.centreX, v.centreY, radius + 50.0);
        }

        for (const auto& t : *in.trains) {
            double z = std::numeric_limits<double>::quiet_NaN();
            double pieceHeading = std::numeric_limits<double>::quiet_NaN();
            if (const auto p = pieces.find(t.trackPiece); p != pieces.end()) {
                const auto a = in.track->vertices.find(p->second->from), b = in.track->vertices.find(p->second->to);
                if (a != in.track->vertices.end() && b != in.track->vertices.end()) {
                    z = (a->second.z + b->second.z) / 2.0;
                    pieceHeading = std::atan2(b->second.worldY() - a->second.worldY(), b->second.worldX() - a->second.worldX());
                }
            }
            const auto& vs = t.vehicles;
            for (std::size_t i = 0; i < vs.size(); ++i) {
                const auto& veh = vs[i];
                const double x = worldOf(veh.boardX, veh.x), y = worldOf(veh.boardY, veh.y);
                double vz = std::isfinite(z) ? z : veh.z;
                const auto here = onTrack ? onTrack->nearest(x, y, kOnTrackDistance) : std::nullopt;
                if (here) {
                    vz = here->z;  // the height of the track under it (still a preview approximation)
                }
                if (!near(x, y) || !std::isfinite(vz)) {
                    continue;
                }
                // PREVIEW: a vehicle's stored yaw is 0 on the disc, so its heading is taken
                // from its neighbours in the train (or the track piece when it is alone);
                // front and back are not known. Train models are long along their own +y
                // (e.g. the 70 t ore car: y -3.7..3.7, x -1.6..1.6), so yaw = heading - 90 deg.
                const auto& prev = vs[i > 0 ? i - 1 : i];
                const auto& next = vs[i + 1 < vs.size() ? i + 1 : i];
                double heading = pieceHeading;
                if (&prev != &next) {
                    heading = std::atan2(worldOf(next.boardY, next.y) - worldOf(prev.boardY, prev.y),
                                         worldOf(next.boardX, next.x) - worldOf(prev.boardX, prev.x));
                }
                if (here) {  // the track's direction under the vehicle, the way round the train runs
                    heading = std::isfinite(heading) ? headingAlong(here->heading, heading) : here->heading;
                }
                if (in.meshes != nullptr && std::isfinite(heading)) {
                    if (const PreparedMesh* m = in.meshes->get(veh.kuid); m != nullptr && fits(v.scene, *m)) {
                        const double yaw = heading - std::numbers::pi / 2;
                        addPlacedMesh(v.scene, *m, x, y, vz, yaw);
                        ++v.vehiclesAsMeshes;
                        v.vehiclesOnTrack += here ? 1 : 0;
                        // Bogies on the body's "a.bog<N>" points (see PreparedMesh::bogeyFor).
                        for (const auto& [name, frame] : m->attachments) {
                            const auto b = m->bogeyFor(name);
                            const PreparedMesh* bogie = b ? in.meshes->get(b->kuid) : nullptr;
                            if (bogie == nullptr || !fits(v.scene, *bogie)) {
                                continue;
                            }
                            // The point's own rotation turns the bogie; "reversed" only reverses
                            // the wheel animation (documented), so it adds no turn here. On the
                            // modelled track, the bogie also turns to the track under its point.
                            LocalFrame inner = frame;
                            const double c = std::cos(yaw), s = std::sin(yaw);
                            const double bx = x + c * frame.t[0] - s * frame.t[1], by = y + s * frame.t[0] + c * frame.t[1];
                            if (const auto under = onTrack ? onTrack->nearest(bx, by, kOnTrackDistance) : std::nullopt) {
                                const double turn = headingAlong(under->heading, heading) - heading;  // within +-90 degrees
                                LocalFrame swivel;
                                swivel.r[0] = swivel.r[4] = static_cast<float>(std::cos(turn));
                                swivel.r[1] = static_cast<float>(std::sin(turn));
                                swivel.r[3] = -swivel.r[1];
                                inner = swivel.then(frame);
                                ++v.bogiesOnTrack;
                            }
                            addPlacedMesh(v.scene, *bogie, inner, x, y, vz, yaw);
                            ++v.bogiesAsMeshes;
                        }
                        continue;
                    }
                }
                v.scene.dots.push_back({{x, y, vz + 2.0}, Rgb{255, 140, 0}, 3});
            }
        }
    }

    // Camera: south-west of the centre, looking at it from above.
    v.camera.target = {v.centreX, v.centreY, centreZ};
    v.camera.eye = {v.centreX - 0.55 * radius, v.centreY - 0.85 * radius, centreZ + 0.45 * radius};
    v.camera.fovYDegrees = 50;
    return v;
}

}  // namespace trainz::view
