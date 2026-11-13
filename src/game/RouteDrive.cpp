#include "RouteDrive.h"

#include "content/TrackLayout.h"

#include <algorithm>
#include <string>
#include <cstring>
#include <cmath>
#include <numbers>

namespace trainz::game {

namespace {
// The name in a signal's record: after 8 or 9 zero bytes a u32 length and the text, ending
// where the 23-byte track attachment starts (observed). "" if it doesn't fit.
std::string signalName(const trainz::content::PlacedObject& o) {
    const auto& r = o.rest;
    if (r.size() < 23 + 12) {
        return {};
    }
    const std::size_t tail = r.size() - 23;
    for (std::size_t at : {std::size_t{8}, std::size_t{9}}) {
        std::uint32_t n = 0;
        std::memcpy(&n, r.data() + at, 4);
        if (at + 4 + n == tail) {
            std::string name(r.begin() + static_cast<std::ptrdiff_t>(at + 4), r.begin() + static_cast<std::ptrdiff_t>(tail));
            name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
            return name;
        }
    }
    return {};
}
}  // namespace

std::vector<trainz::sim::Signal> routeSignals(const trainz::sim::TrackNetwork& net,
                                              const std::vector<trainz::content::PlacedObject>& objects) {
    std::vector<trainz::sim::Signal> out;
    for (const auto& o : objects) {
        if (o.tag != "sgOb") {
            continue;
        }
        const auto a = trainz::content::trackAttachment(o);
        const auto len = a ? net.length(a->piece) : std::nullopt;
        if (!a || !len || a->distance < -0.01 || a->distance > *len + 0.01) {
            continue;
        }
        const double d = std::clamp(static_cast<double>(a->distance), 0.0, *len);
        out.push_back({trainz::sim::TrackPosition{a->piece, a->alongPiece ? d : *len - d, a->alongPiece ? +1 : -1}, signalName(o),
                       static_cast<long>(&o - objects.data())});
    }
    return out;
}

std::size_t orientSignals(const trainz::sim::TrackNetwork& net, std::vector<trainz::content::PlacedObject>& objects) {
    std::size_t turned = 0;
    for (auto& o : objects) {
        if (o.tag != "sgOb") {
            continue;
        }
        const auto a = trainz::content::trackAttachment(o);
        const auto len = a ? net.length(a->piece) : std::nullopt;
        if (!a || !len) {
            continue;
        }
        const double d = std::clamp(static_cast<double>(a->distance), 0.0, *len);
        const auto p = net.point({a->piece, a->alongPiece ? d : *len - d, a->alongPiece ? +1 : -1});
        if (!p) {
            continue;
        }
        o.yaw = static_cast<float>(p->heading - std::numbers::pi / 2);
        ++turned;
    }
    return turned;
}

std::vector<SignalLamp> signalLamps(const trainz::content::PlacedObject& o, const trainz::view::PreparedMesh& mesh,
                                    const trainz::content::SignalLook& look, int aspect) {
    std::vector<SignalLamp> out;
    const double c = std::cos(o.yaw), s = std::sin(o.yaw);
    auto place = [&](const float* t, std::array<std::uint8_t, 3> rgb) {
        out.push_back({o.worldX() + c * t[0] - s * t[1], o.worldY() + s * t[0] + c * t[1], o.z + t[2], rgb});
    };
    const auto state = trainz::content::stateForAspect(look, aspect);
    if (!state) {
        // No `signals` list: the older a.red / a.orange (or a.yellow) / a.green points (the Trainz
        // wiki's pre-v1.3 fallback; 70 of the disc's 637 signal assets have no list).
        static const char* const kNames[3][2] = {{"a.red", "a.red"}, {"a.orange", "a.yellow"}, {"a.green", "a.green"}};
        static const std::array<std::uint8_t, 3> kRgb[3] = {{255, 40, 30}, {255, 190, 30}, {60, 255, 90}};
        const int k = std::clamp(aspect, 0, 2);
        for (const char* name : kNames[k]) {
            if (const auto at = mesh.attachments.find(name); at != mesh.attachments.end()) {
                place(at->second.t, kRgb[k]);
                break;
            }
        }
        return out;
    }
    for (const int light : look.states.at(*state)) {
        const auto at = mesh.attachments.find("a.light" + std::to_string(light));
        if (light < 0 || at == mesh.attachments.end()) {
            continue;
        }
        const auto corona = look.coronas.find(light);
        place(at->second.t, trainz::content::coronaColour(corona == look.coronas.end() ? std::string() : corona->second));
    }
    return out;
}

std::vector<trainz::sim::Occupied> trainOccupies(const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& lead,
                                                 double length, double step) {
    std::vector<trainz::sim::Occupied> out{{lead.piece, lead.offset}};
    if (!(length > 0) || !(step > 0) || !std::isfinite(length)) {
        return out;
    }
    trainz::sim::TrackPosition p = lead;
    for (double done = 0; done < length;) {
        const double s = std::min(step, length - done);
        const auto m = net.advance(p, -s);
        out.push_back({p.piece, p.offset});
        if (m.stoppedAtEnd || std::fabs(m.moved) < 1e-9) {
            break;
        }
        done += s;
    }
    return out;
}

std::vector<trainz::sim::Occupied> standingTrains(const trainz::sim::TrackNetwork& net,
                                                  const std::vector<trainz::content::Train>& trains, long except) {
    std::vector<trainz::sim::Occupied> out;
    for (std::size_t t = 0; t < trains.size(); ++t) {
        if (static_cast<long>(t) == except) {
            continue;
        }
        for (const auto& v : trains[t].vehicles) {
            const double x = v.boardX * trainz::content::kBaseboardSize + v.x, y = v.boardY * trainz::content::kBaseboardSize + v.y;
            if (const auto at = net.locate(x, y, 5.0)) {
                out.push_back({at->piece, at->offset});
            }
        }
    }
    return out;
}

trainz::sim::VehicleSpec ownVehicleSpec(std::optional<double> massKg, const trainz::content::EngineSpec* engine) {
    const int axles = engine != nullptr && engine->axleCount && *engine->axleCount >= 2 ? *engine->axleCount : 4;
    const bool massOk = massKg && std::isfinite(*massKg) && *massKg >= 1000 && *massKg <= 200'000;
    auto v = trainz::sim::freightWagon(massOk ? *massKg / 1000 : 40, axles);
    v.name = "vehicle";
    v.source = massOk ? "mass from its config; " + v.source : "no usable mass: 40 t preset; " + v.source;
    if (engine != nullptr) {
        v.tractiveCurveN = engine->fullThrottleCurveN();
        if (!v.tractiveCurveN.empty()) {
            for (const auto& notch : engine->throttlePower) {  // kN -> N, as tractiveForceN
                auto& out = v.notchCurvesN.emplace_back();
                for (const auto& [speed, kn] : notch) {
                    out.push_back({speed, 1000.0 * kn});
                }
            }
            for (const auto& notch : engine->dynamicBrake) {  // kN -> N
                auto& out = v.dynamicBrakeCurvesN.emplace_back();
                for (const auto& [speed, kn] : notch) {
                    out.push_back({speed, 1000.0 * kn});
                }
            }
        }
        v.driven = !v.tractiveCurveN.empty();
        if (v.driven) {
            v.source += "; traction from its enginespec's top throttle-power notch (kN read as written)";
        }
    }
    return v;
}

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
