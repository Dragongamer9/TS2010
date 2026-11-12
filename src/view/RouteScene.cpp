#include "RouteScene.h"

#include "content/ContentError.h"

#include <algorithm>
#include <array>
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

std::optional<BogiePlacement> placeOnBogies(const TrackLocator& track, double x, double y, double heading,
                                            const float local0[2], const float local1[2]) {
    const double lx = local1[0] - local0[0], ly = local1[1] - local0[1];
    const double span = std::hypot(lx, ly);
    if (!(span > 0.5) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(heading)) {
        return std::nullopt;
    }
    const double midX = 0.5 * (local0[0] + local1[0]), midY = 0.5 * (local0[1] + local1[1]);
    const double localAngle = std::atan2(ly, lx);  // of point 0 -> point 1 in the body (pi/2 for +y)
    BogiePlacement p{x, y, 0, heading, 0};
    // Where a body point (lx, ly, 0) goes: tilted by pitch about the body's x axis, turned by
    // yaw = heading - pi/2 (its +y along the heading, as for vehicles in buildRouteView), moved.
    auto offset = [](double bodyX, double bodyY, double yawAngle, double pitch) {
        const double ty = bodyY * std::cos(pitch), tz = bodyY * std::sin(pitch);
        const double c = std::cos(yawAngle), s = std::sin(yawAngle);
        return std::array<double, 3>{c * bodyX - s * ty, s * bodyX + c * ty, tz};
    };
    for (int pass = 0; pass < 4; ++pass) {
        const double yaw = p.heading - std::numbers::pi / 2;
        auto world = [&](const float l[2]) {
            const auto o = offset(l[0], l[1], yaw, p.pitch);
            return std::pair{p.x + o[0], p.y + o[1]};
        };
        const auto [ax, ay] = world(local0);
        const auto [bx, by] = world(local1);
        const auto a = track.nearest(ax, ay, kOnTrackDistance), b = track.nearest(bx, by, kOnTrackDistance);
        if (!a || !b) {
            return std::nullopt;
        }
        const double dx = b->x - a->x, dy = b->y - a->y, flat = std::hypot(dx, dy);
        if (!(flat > 0.25 * span)) {
            return std::nullopt;
        }
        // The body turns so that point 0 -> point 1 lies along a -> b, and tilts with them:
        // nose (+y end) up when the track is higher under the point lying toward +y.
        p.heading = headingAlong(std::atan2(dy, dx) - localAngle + std::numbers::pi / 2, p.heading);
        const double rise = (b->z - a->z) * (ly >= 0 ? 1.0 : -1.0);
        p.pitch = std::atan2(rise, flat);
        // Then the points' midpoint, tilted and turned, goes to the middle of a and b (all three
        // coordinates, so off-centre points don't lift or shift the body on a slope).
        const auto o = offset(midX, midY, p.heading - std::numbers::pi / 2, p.pitch);
        p.x = 0.5 * (a->x + b->x) - o[0];
        p.y = 0.5 * (a->y + b->y) - o[1];
        p.z = 0.5 * (a->z + b->z) - o[2];
    }
    return p;
}

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
        // Ground textures: the section's own blend, baked into one image (GroundTextures.h).
        // Version 12 files remap texture numbers through a world table (not read): not textured.
        int baked = -1;
        if (in.groundTextures != nullptr && g.version >= 13) {
            std::vector<const SceneTexture*> slots(d.textures.size(), nullptr);
            bool any = false;
            for (std::size_t k = 0; k < d.textures.size(); ++k) {
                if (d.textures[k].used) {
                    slots[k] = in.groundTextures->get(d.textures[k].kuid);
                    any = any || slots[k] != nullptr;
                }
            }
            if (any) {
                const std::uint32_t size = radius <= 300 ? 1024 : 512;
                auto fallback = [&](double col, double row) {
                    return colour(std::min(cells, static_cast<std::size_t>(row)), std::min(cells, static_cast<std::size_t>(col)));
                };
                v.ownedTextures.push_back(std::make_unique<SceneTexture>(bakeGroundSection(d, side, slots, size, fallback)));
                baked = static_cast<int>(v.scene.textures.size());
                v.scene.textures.push_back(v.ownedTextures.back().get());
                ++v.texturedSections;
            }
        }
        const float uvStep = 1.0F / static_cast<float>(cells);
        for (std::size_t r = 0; r < cells; ++r) {
            for (std::size_t c = 0; c < cells; ++c) {
                const ScenePoint p00 = point(r, c), p01 = point(r, c + 1), p10 = point(r + 1, c), p11 = point(r + 1, c + 1);
                if (!near(p00.x, p00.y) && !near(p11.x, p11.y)) {
                    continue;
                }
                if (!std::isfinite(p00.z) || !std::isfinite(p01.z) || !std::isfinite(p10.z) || !std::isfinite(p11.z)) {
                    continue;
                }
                if (baked >= 0) {
                    const Rgb w{255, 255, 255};
                    const float u0 = static_cast<float>(c) * uvStep, u1 = static_cast<float>(c + 1) * uvStep;
                    const float v0 = static_cast<float>(r) * uvStep, v1 = static_cast<float>(r + 1) * uvStep;
                    SceneTriangle a{{p00, p01, p11}, {w, w, w}, baked, {u0, u1, u1}, {v0, v0, v1}};
                    SceneTriangle b{{p00, p11, p10}, {w, w, w}, baked, {u0, u1, u0}, {v0, v1, v1}};
                    v.scene.triangles.push_back(a);
                    v.scene.triangles.push_back(b);
                } else {
                    const Rgb c00 = colour(r, c), c01 = colour(r, c + 1), c10 = colour(r + 1, c), c11 = colour(r + 1, c + 1);
                    v.scene.triangles.push_back({{p00, p01, p11}, {c00, c01, c11}});
                    v.scene.triangles.push_back({{p00, p11, p10}, {c00, c11, c10}});
                }
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
                        // On its bogies where it can be (placeOnBogies): position, direction, tilt.
                        double px = x, py = y, pz = vz, ph = heading;
                        LocalFrame tilt;
                        const auto b0 = m->attachments.find("a.bog0"), b1 = m->attachments.find("a.bog1");
                        if (onTrack && here && b0 != m->attachments.end() && b1 != m->attachments.end()) {
                            const float l0[2] = {b0->second.t[0], b0->second.t[1]}, l1[2] = {b1->second.t[0], b1->second.t[1]};
                            if (const auto pl = placeOnBogies(*onTrack, x, y, heading, l0, l1)) {
                                px = pl->x, py = pl->y, pz = pl->z, ph = pl->heading;
                                const float cp = static_cast<float>(std::cos(pl->pitch)), sp = static_cast<float>(std::sin(pl->pitch));
                                tilt.r[4] = cp, tilt.r[5] = sp, tilt.r[7] = -sp, tilt.r[8] = cp;  // +y nose up about x
                                ++v.vehiclesOnBogies;
                            }
                        }
                        const double yaw = ph - std::numbers::pi / 2;
                        addPlacedMesh(v.scene, *m, tilt, px, py, pz, yaw);
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
                            float tilted[3];  // the point where it is drawn: tilted with the body
                            tilt.apply(frame.t, tilted);
                            const double bx = px + c * tilted[0] - s * tilted[1], by = py + s * tilted[0] + c * tilted[1];
                            if (const auto under = onTrack ? onTrack->nearest(bx, by, kOnTrackDistance) : std::nullopt) {
                                const double turn = headingAlong(under->heading, ph) - ph;  // within +-90 degrees
                                inner = swivelAboutModelZ(frame, turn);  // about the body's vertical
                                ++v.bogiesOnTrack;
                            }
                            addPlacedMesh(v.scene, *bogie, inner.then(tilt), px, py, pz, yaw);
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
