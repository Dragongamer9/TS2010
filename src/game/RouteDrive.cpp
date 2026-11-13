#include "RouteDrive.h"

#include "content/TrackLayout.h"

#include <algorithm>
#include <string>
#include <cstring>
#include <map>
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

std::optional<RouteTrain> makeRouteTrain(const trainz::sim::TrackNetwork& net, const std::vector<trainz::content::Train>& trains,
                                         std::size_t index, trainz::view::AssetMeshes& meshes) {
    if (index >= trains.size()) {
        return std::nullopt;
    }
    const auto where = trainStart(net, trains[index]);
    if (!where) {
        return std::nullopt;
    }
    RouteTrain t;
    t.index = index;
    std::vector<trainz::sim::VehicleSpec> specs;
    for (const auto& v : trains[index].vehicles) {
        // Its own physics data: the traincar's mass and its enginespec's curves.
        std::optional<double> mass;
        std::optional<trainz::content::EngineSpec> engine;
        if (const auto cfg = meshes.config(v.kuid)) {
            if (const auto* m = cfg->child("mass"); m != nullptr && (!m->floats.empty() || !m->ints.empty())) {
                mass = m->floats.empty() ? m->ints[0] : m->floats[0];
            }
            if (const auto* es = cfg->child("enginespec")) {
                if (const auto ek = trainz::view::configKuid(*es)) {
                    if (const auto ecfg = meshes.config(*ek)) {
                        engine = trainz::content::parseEngineSpec(*ecfg);
                    }
                }
            }
        }
        t.withOwnMass += mass ? 1 : 0;
        specs.push_back(ownVehicleSpec(mass, engine ? &*engine : nullptr));
        t.powered += specs.back().driven ? 1 : 0;
        t.cars.push_back({specs.back(), std::nullopt});
        // Drawn only with a model the port can read; its mass, brakes and pull count anyway.
        const auto* body = meshes.get(v.kuid);
        if (body == nullptr) {
            continue;
        }
        const trainz::view::PreparedMesh* bog[2] = {nullptr, nullptr};
        for (int k = 0; k < 2; ++k) {
            if (const auto b = body->bogeyFor(k == 0 ? "a.bog0" : "a.bog1")) {
                bog[k] = meshes.get(b->kuid);
            }
        }
        t.cars.back().vehicle = routeVehicle(*body, bog[0], bog[1]);
    }
    t.vehicleCount = specs.size();
    t.drive.lead = *where;
    t.fallbackPower = std::none_of(specs.begin(), specs.end(), [](const trainz::sim::VehicleSpec& spec) {
        return !spec.tractiveCurveN.empty() || spec.tractiveForceN > 0;
    });
    refreshTrain(t);
    if (t.vehicles.empty()) {
        return std::nullopt;
    }
    return t;
}

void refreshTrain(RouteTrain& t) {
    t.vehicles.clear();
    t.length = 0;
    std::vector<trainz::sim::VehicleSpec> specs;
    bool anyCurves = false;
    for (const auto& c : t.cars) {
        specs.push_back(c.spec);
        anyCurves = anyCurves || !c.spec.tractiveCurveN.empty() || c.spec.tractiveForceN > 0;
        if (c.vehicle) {
            t.vehicles.push_back(*c.vehicle);
            t.length += c.vehicle->length + 0.5;
        }
    }
    if (t.fallbackPower && !anyCurves && !specs.empty()) {
        // Supply traction only; keep the car's mass, resistance and brakes intact.
        const auto fallback = trainz::sim::electricLocomotive();
        specs.front().tractiveForceN = fallback.tractiveForceN;
        specs.front().powerW = fallback.powerW;
        specs.front().driven = true;
    }
    t.drive.spec = trainz::sim::makeTrain(specs);
}

namespace {
double firstHalf(const RouteTrain& t) { return t.vehicles.empty() ? 0.0 : 0.5 * t.vehicles.front().length; }
double lastHalf(const RouteTrain& t) { return t.vehicles.empty() ? 0.0 : 0.5 * t.vehicles.back().length; }
// From the lead (first middle) back to the rear end: the drawn length less the trailing gap
// and the first vehicle's front half.
double leadToRearEnd(const RouteTrain& t) { return t.vehicles.empty() ? 0.0 : t.length - 0.5 - firstHalf(t); }
}  // namespace

trainz::sim::TrackPosition trainFrontEnd(const trainz::sim::TrackNetwork& net, const RouteTrain& t) {
    trainz::sim::TrackPosition p = t.drive.lead;
    net.advance(p, firstHalf(t));
    return p;
}

trainz::sim::TrackPosition trainRearEnd(const trainz::sim::TrackNetwork& net, const RouteTrain& t) {
    trainz::sim::TrackPosition p = t.drive.lead;
    net.advance(p, -leadToRearEnd(t));
    return p;
}

std::vector<trainz::sim::Occupied> trainOccupies(const trainz::sim::TrackNetwork& net, const RouteTrain& t, double step) {
    if (t.vehicles.empty()) {
        return {};
    }
    // length includes one trailing display gap; the physical buffers do not.
    return trainOccupies(net, trainFrontEnd(net, t), std::max(0.0, t.length - 0.5), step);
}

trainz::sim::TrackPosition lastVehicleMiddle(const trainz::sim::TrackNetwork& net, const RouteTrain& t) {
    trainz::sim::TrackPosition p = t.drive.lead;
    net.advance(p, -(leadToRearEnd(t) - lastHalf(t)));
    return p;
}

std::optional<Contact> findContact(const trainz::sim::TrackNetwork& net, const RouteTrain& me, const std::vector<RouteTrain>& others,
                                   double within, bool atOurFront) {
    std::optional<Contact> best;
    // Our two buffer ends, each looking outward along the track.
    const trainz::sim::TrackPosition frontOut = trainFrontEnd(net, me);
    trainz::sim::TrackPosition rearOut = trainRearEnd(net, me);
    rearOut.direction = -rearOut.direction;
    for (const bool front : {true, false}) {
        if (front != atOurFront) {
            continue;
        }
        const trainz::sim::TrackPosition& from = front ? frontOut : rearOut;
        net.walk(from, within, [&](const trainz::sim::TrackNetwork::Span& s) {
            const double lo = std::min(s.from, s.to), hi = std::max(s.from, s.to);
            for (std::size_t i = 0; i < others.size(); ++i) {
                if (others[i].cars.empty()) {
                    continue;
                }
                const trainz::sim::TrackPosition ends[2] = {trainFrontEnd(net, others[i]), trainRearEnd(net, others[i])};
                for (int e = 0; e < 2; ++e) {
                    if (ends[e].piece != s.piece || ends[e].offset < lo - 1e-6 || ends[e].offset > hi + 1e-6) {
                        continue;
                    }
                    const double gap = s.startDistance + std::fabs(ends[e].offset - s.from);
                    if (!best || gap < best->gap) {
                        best = Contact{i, front, e == 0, gap};
                    }
                }
            }
            return true;
        });
    }
    return best;
}

namespace {
std::vector<RouteTrain::Car> turnedRound(std::vector<RouteTrain::Car> cars) {
    std::reverse(cars.begin(), cars.end());
    for (auto& c : cars) {
        if (c.vehicle) {
            c.vehicle->flipped = !c.vehicle->flipped;
        }
    }
    return cars;
}
}  // namespace

void coupleTrains(const trainz::sim::TrackNetwork& net, RouteTrain& me, RouteTrain& other, const Contact& contact) {
    const double myMass = me.drive.spec.massKg, theirMass = other.drive.spec.massKg;
    std::vector<RouteTrain::Car> theirs = other.cars;
    std::vector<RouteTrain::Car> merged;
    if (contact.atOurFront) {
        // Their cars go in front of ours. Touching their rear, they already face our way and
        // their lead is the new lead; touching their front, they are turned round and the new
        // lead is their last vehicle's middle, facing away from us.
        trainz::sim::TrackPosition newLead = other.drive.lead;
        if (contact.theirFront) {
            theirs = turnedRound(std::move(theirs));
            newLead = lastVehicleMiddle(net, other);
            newLead.direction = -newLead.direction;
        }
        merged = std::move(theirs);
        merged.insert(merged.end(), me.cars.begin(), me.cars.end());
        me.drive.lead = newLead;
    } else {
        // Their cars go behind ours: as they are if their front touches our rear, else turned round.
        if (!contact.theirFront) {
            theirs = turnedRound(std::move(theirs));
        }
        merged = me.cars;
        merged.insert(merged.end(), theirs.begin(), theirs.end());
    }
    me.cars = std::move(merged);
    me.fallbackPower = me.fallbackPower || other.fallbackPower;
    me.vehicleCount += other.vehicleCount;
    me.drive.speed = myMass + theirMass > 0 ? me.drive.speed * myMass / (myMass + theirMass) : 0;  // they stood
    refreshTrain(me);
    other.cars.clear();
    other.fallbackPower = false;
    other.vehicles.clear();
}

std::optional<RouteTrain> uncoupleRear(const trainz::sim::TrackNetwork& net, RouteTrain& me, std::size_t count) {
    if (me.cars.size() < 2 || count == 0) {
        return std::nullopt;
    }
    count = std::min(count, me.cars.size() - 1);
    RouteTrain rest;
    rest.cars.assign(me.cars.end() - static_cast<std::ptrdiff_t>(count), me.cars.end());
    me.cars.erase(me.cars.end() - static_cast<std::ptrdiff_t>(count), me.cars.end());
    me.vehicleCount = me.cars.size();
    refreshTrain(me);
    refreshTrain(rest);
    rest.vehicleCount = rest.cars.size();
    rest.index = me.index;
    // Its first vehicle's middle: behind our new rear end by the coupling gap and half itself.
    rest.drive.lead = trainRearEnd(net, me);
    net.advance(rest.drive.lead, -(0.5 + firstHalf(rest)));
    rest.drive.speed = 0;
    rest.drive.controls.brake = 1;
    rest.parked = true;
    return rest;
}

std::optional<AiDriver::StopAhead> AiDriver::stopAhead(const RouteTrain& d, const trainz::sim::TrackNetwork& net,
                                                       const trainz::sim::SignalSystem* signals,
                                                       const std::vector<trainz::sim::Occupied>& others, double look) const {
    std::optional<StopAhead> best;
    auto consider = [&](double dist, const char* why) {
        if (dist >= 0 && (!best || dist < best->distance)) {
            best = StopAhead{dist, why};
        }
    };
    if (signals != nullptr) {
        if (const auto sg = signals->nextSignal(d.drive.lead, look); sg && signals->aspect(sg->index) == trainz::sim::Aspect::Stop) {
            consider(sg->distance, "signal");
        }
    }
    std::multimap<std::int32_t, double> taken;
    for (const auto& o : others) {
        taken.emplace(o.piece, o.offset);
    }
    double walked = 0;
    bool ended = true;
    net.walk(d.drive.lead, look, [&](const trainz::sim::TrackNetwork::Span& s) {
        const auto [b, e] = taken.equal_range(s.piece);
        for (auto it = b; it != e; ++it) {
            const double along = (it->second - s.from) * s.direction;
            if (along >= 0 && along <= std::fabs(s.to - s.from)) {
                consider(s.startDistance + along, "train");
            }
        }
        walked = s.startDistance + std::fabs(s.to - s.from);
        return true;
    });
    ended = walked < look - 1e-6;
    if (ended) {
        consider(walked, "line end");
    }
    return best;
}

void AiDriver::control(RouteTrain& d, const trainz::sim::TrackNetwork& net, const trainz::sim::SignalSystem* signals,
                       const std::vector<trainz::sim::Occupied>& others) const {
    auto& c = d.drive.controls;
    const double v = std::fabs(d.drive.speed);
    c.reverser = +1;
    c.dynamicNotch = 0;
    if (d.heldRedSignal && signals != nullptr &&
        *d.heldRedSignal < signals->signals().size() &&
        signals->aspect(*d.heldRedSignal) != trainz::sim::Aspect::Stop) {
        d.heldRedSignal.reset();
    }
    double limit = cruise;
    if (signals != nullptr) {
        if (const auto sg = signals->nextSignal(d.drive.lead, 1500)) {
            const auto aspect = signals->aspect(sg->index);
            if (aspect == trainz::sim::Aspect::Caution) {
                limit = cruise / 2;
            } else if (aspect == trainz::sim::Aspect::Stop && sg->distance <= margin && !d.heldRedSignal) {
                // Remember before crossing: an overrun removes this signal from nextSignal.
                d.heldRedSignal = sg->index;
            }
        }
    }
    if (d.heldRedSignal) {
        c.notch = 0, c.throttle = 0, c.brake = 1;
        return;
    }
    const auto stop = stopAhead(d, net, signals, others);
    bool fullBrake = false;
    if (stop) {
        // 1 m more for the last step: the brake works in steps (0.5 s in the viewer's test runs).
        const double room = stop->distance - margin - 1.0;
        // Use the same brake-force and adhesion limits as stepTrain. Ignore resistance
        // (which helps stopping), and reserve 20% of the available deceleration.
        const auto& spec = d.drive.spec;
        constexpr double gravity = 9.81;
        double downhill = 0;
        net.walk(d.drive.lead, stop->distance, [&](const trainz::sim::TrackNetwork::Span& s) {
            for (const double offset : {s.from, (s.from + s.to) * 0.5, s.to}) {
                if (const auto p = net.point({s.piece, offset, s.direction})) {
                    downhill = std::max(downhill, -p->grade);
                }
            }
            return true;
        });
        const double grip = std::clamp(spec.adhesion, 0.0, 1.0) * gravity *
                            std::clamp(spec.brakedMassKg, 0.0, spec.massKg);
        const double available = spec.massKg > 0
                                     ? std::min(std::max(0.0, spec.maxBrakeForceN), grip) / spec.massKg - gravity * downhill
                                     : 0;
        const double planned = std::max(0.0, std::min(decel, 0.8 * available));
        const double envelope = room > 0 ? std::sqrt(2 * planned * room) : 0.0;  // fastest it may be here to stop in time
        limit = std::min(limit, envelope);
        // The stopping envelope assumes braking, not a small proportional demand. Compared with the
        // envelope itself, not the cruise-capped limit, so cruising toward a far stop does not brake.
        fullBrake = room <= 0 || planned <= 0 || v >= envelope - 0.5;
    }
    if (fullBrake) {
        c.notch = 0, c.throttle = 0, c.brake = 1;
        return;
    }
    const int topNotch = static_cast<int>(d.drive.spec.notchCurvesN.size()) - 1;
    if (v > limit + 0.3) {  // too fast: brake in proportion to the excess, no power
        c.notch = 0, c.throttle = 0;
        c.brake = std::clamp((v - limit) / 2.0 + 0.3, 0.0, 1.0);
    } else if (v < limit - 0.5) {  // power on: a middle notch, gentle
        c.brake = 0;
        c.notch = topNotch > 0 ? std::max(1, topNotch / 2) : 0;
        c.throttle = 0.5;
    } else {  // coast
        c.notch = 0, c.throttle = 0, c.brake = limit < 0.1 ? 1.0 : 0.0;  // hold it when stopped
    }
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
        const double turn = v.flipped ? std::numbers::pi : 0.0;  // a vehicle turned round in its train
        const double sign = v.flipped ? -1.0 : 1.0;
        if (!v.onBogies) {
            addPlacedMesh(scene, *v.body, c->x, c->y, c->z, c->heading - std::numbers::pi / 2 + turn);
            ++placed;
            continue;
        }
        // Each bogie point on the track at its distance from the middle (+y is forward).
        std::array<TrackPoint, 2> pt{};
        bool ok = true;
        for (int k = 0; k < 2; ++k) {
            TrackPosition p = middle;
            net.advance(p, sign * v.bogieY[static_cast<std::size_t>(k)]);
            const auto q = net.point(p);
            ok = ok && q.has_value();
            if (q) {
                pt[static_cast<std::size_t>(k)] = *q;
            }
        }
        const auto& ahead = sign * v.bogieY[0] > sign * v.bogieY[1] ? pt[0] : pt[1];
        const auto& behind = sign * v.bogieY[0] > sign * v.bogieY[1] ? pt[1] : pt[0];
        const double dx = ahead.x - behind.x, dy = ahead.y - behind.y, flat = std::hypot(dx, dy);
        if (!ok || flat < 0.5) {
            break;  // squashed against a buffer
        }
        const double heading = std::atan2(dy, dx), yaw = heading - std::numbers::pi / 2 + turn;
        // In the model's own frame a turned-round vehicle climbs the other way.
        const double pitch = sign * std::atan2(ahead.z - behind.z, flat);
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
