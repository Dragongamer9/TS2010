#include "RouteDrive.h"

#include "content/TrackLayout.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace trainz::game {

using namespace trainz::content;
using namespace trainz::sim;
using namespace trainz::view;

std::optional<TrackPosition> trainStart(const TrackNetwork& net, const Train& train, double maxDistance) {
    if (train.vehicles.empty()) {
        return std::nullopt;
    }
    auto world = [](const Vehicle& v) { return std::pair{v.boardX * kBaseboardSize + v.x, v.boardY * kBaseboardSize + v.y}; };
    const auto [x, y] = world(train.vehicles.front());
    auto p = net.locate(x, y, maxDistance);
    if (!p || train.vehicles.size() < 2) {
        return p;
    }
    // Face away from the second vehicle: compare the track's direction with first - second.
    const auto [x2, y2] = world(train.vehicles[1]);
    const auto here = net.point(*p);
    if (here && std::cos(here->heading) * (x - x2) + std::sin(here->heading) * (y - y2) < 0) {
        p->direction = -1;
    }
    return p;
}

RouteVehicle routeVehicle(const PreparedMesh& body, const PreparedMesh* bogie0, const PreparedMesh* bogie1) {
    RouteVehicle v;
    v.body = &body;
    float lo = 1e9F, hi = -1e9F;
    for (const auto& t : body.triangles) {
        for (const auto& p : t.p) {
            lo = std::min(lo, p[1]), hi = std::max(hi, p[1]);
        }
    }
    if (hi > lo) {
        v.length = hi - lo;
    }
    const auto a = body.attachments.find("a.bog0"), b = body.attachments.find("a.bog1");
    if (a != body.attachments.end() && b != body.attachments.end() && std::fabs(a->second.t[1] - b->second.t[1]) > 0.5) {
        v.bogieY = {a->second.t[1], b->second.t[1]};
        v.onBogies = true;
        v.bogies = {bogie0, bogie1};
    }
    return v;
}

int addRouteTrain(Scene& scene, const TrackNetwork& net, const TrackPosition& front, const std::vector<RouteVehicle>& vehicles,
                  double gap) {
    TrackPosition middle = front;
    int placed = 0;
    for (std::size_t i = 0; i < vehicles.size(); ++i) {
        const auto& v = vehicles[i];
        if (v.body == nullptr) {
            continue;
        }
        if (i > 0) {  // from the last vehicle's middle to this one's
            net.advance(middle, -(0.5 * vehicles[i - 1].length + gap + 0.5 * v.length));
        }
        const auto c = net.point(middle);
        if (!c) {
            break;
        }
        if (!v.onBogies) {
            addPlacedMesh(scene, *v.body, c->x, c->y, c->z, c->heading - std::numbers::pi / 2);
            ++placed;
            continue;
        }
        // Each bogie point on the track at its distance from the middle (+y is forward).
        std::array<TrackPoint, 2> pt{};
        bool ok = true;
        for (int k = 0; k < 2; ++k) {
            TrackPosition p = middle;
            net.advance(p, v.bogieY[static_cast<std::size_t>(k)]);
            const auto q = net.point(p);
            ok = ok && q.has_value();
            if (q) {
                pt[static_cast<std::size_t>(k)] = *q;
            }
        }
        const auto& ahead = v.bogieY[0] > v.bogieY[1] ? pt[0] : pt[1];
        const auto& behind = v.bogieY[0] > v.bogieY[1] ? pt[1] : pt[0];
        const double dx = ahead.x - behind.x, dy = ahead.y - behind.y, flat = std::hypot(dx, dy);
        if (!ok || flat < 0.5) {
            break;  // squashed against a buffer
        }
        const double heading = std::atan2(dy, dx), yaw = heading - std::numbers::pi / 2;
        const double pitch = std::atan2(ahead.z - behind.z, flat);
        LocalFrame tilt;
        tilt.r[4] = static_cast<float>(std::cos(pitch)), tilt.r[5] = static_cast<float>(std::sin(pitch));
        tilt.r[7] = -tilt.r[5], tilt.r[8] = tilt.r[4];
        // The body's origin: the points' midpoint less the (tilted, turned) middle of the points.
        const double midY = 0.5 * (v.bogieY[0] + v.bogieY[1]);
        const double ox = -std::sin(yaw) * midY * std::cos(pitch), oy = std::cos(yaw) * midY * std::cos(pitch);
        const double oz = midY * std::sin(pitch);
        addPlacedMesh(scene, *v.body, tilt, 0.5 * (ahead.x + behind.x) - ox, 0.5 * (ahead.y + behind.y) - oy,
                      0.5 * (ahead.z + behind.z) - oz, yaw);
        for (int k = 0; k < 2; ++k) {
            const PreparedMesh* bogie = v.bogies[static_cast<std::size_t>(k)];
            if (bogie != nullptr) {
                const auto& q = pt[static_cast<std::size_t>(k)];
                const double h = std::cos(q.heading - heading) >= 0 ? q.heading : q.heading + std::numbers::pi;
                addPlacedMesh(scene, *bogie, q.x, q.y, q.z, h - std::numbers::pi / 2);
            }
        }
        ++placed;
    }
    return placed;
}

}  // namespace trainz::game
