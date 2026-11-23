#include "RouteDrive.h"

#include "content/TrackLayout.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <cmath>
#include <numbers>

namespace trainz::game {

namespace {
// The name in a trackside record: after 8 or 9 zero bytes a u32 length and the text, ending
// where the 31- or 23-byte track attachment starts (observed). "" if it doesn't fit.
std::string signalName(const trainz::content::PlacedObject& o) {
    const auto& r = o.rest;
    for (const std::size_t tailSize : {std::size_t{31}, std::size_t{23}}) {  // trackmark/trigger form, then the signal form
    if (r.size() < tailSize + 12) {
        continue;
    }
    const std::size_t tail = r.size() - tailSize;
    for (std::size_t at : {std::size_t{8}, std::size_t{9}}) {
        std::uint32_t n = 0;
        std::memcpy(&n, r.data() + at, 4);
        if (at + 4 + n == tail) {
            std::string name(r.begin() + static_cast<std::ptrdiff_t>(at + 4), r.begin() + static_cast<std::ptrdiff_t>(tail));
            name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
            return name;
        }
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
    bool havePressures = false;
    for (const auto& v : trains[index].vehicles) {
        // Its own physics data: the traincar's mass and its enginespec's curves.
        std::optional<double> mass;
        std::optional<trainz::content::EngineSpec> engine;
        bool engineTag = false;  // the traincar's own "engine 1": a locomotive
        if (const auto cfg = meshes.config(v.kuid)) {
            if (const auto* e = cfg->child("engine"); e != nullptr && (!e->ints.empty() || !e->floats.empty())) {
                engineTag = (e->ints.empty() ? e->floats[0] : e->ints[0]) != 0;
            }
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
        if (engine && engine->brakePipe && engine->brakeFull && !havePressures) {
            t.airBrake.setPressures(engine->brakePipe, engine->brakeInitial, engine->brakeFull);
            havePressures = true;
        }
        specs.push_back(ownVehicleSpec(mass, engine ? &*engine : nullptr));
        t.powered += specs.back().driven ? 1 : 0;
        t.cars.push_back({specs.back(), std::nullopt, v.name, engineTag});
        if (const auto cfg = meshes.config(v.kuid)) {
            t.cars.back().cargo = parseQueues(*cfg, [&](const trainz::content::Kuid& k) { return assetProduct(meshes, k); });
        }
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
    // Which cars are engines: those driven by their own data or tagged "engine 1" in their config;
    // none in a train of wagons alone (it is pulled by the borrowed power on its first car if
    // driven at all). Marking a lone brake van as the engine made it the "engine" after coupling.
    for (auto& c : t.cars) {
        c.engine = c.engine || c.spec.driven;
    }
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
        // Supply traction only; keep the car's mass, resistance and brakes intact. To the engine
        // car (its own wheels grip), not whatever car is in front: after running round, that was
        // the brake van, and 20 t of grip could not lift 429 t up a 1.4% grade.
        const auto engine = std::find_if(t.cars.begin(), t.cars.end(), [](const RouteTrain::Car& c) { return c.engine; });
        auto& power = specs[engine == t.cars.end() ? 0 : static_cast<std::size_t>(engine - t.cars.begin())];
        const auto fallback = trainz::sim::electricLocomotive();
        power.tractiveForceN = fallback.tractiveForceN;
        power.powerW = fallback.powerW;
        power.driven = true;
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

trainz::sim::TrackPosition movingEnd(const trainz::sim::TrackNetwork& net, const RouteTrain& t) {
    if (!t.reversing) {
        return trainFrontEnd(net, t);
    }
    trainz::sim::TrackPosition p = trainRearEnd(net, t);
    p.direction = -p.direction;
    return p;
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
                if (others[i].cars.empty() || &others[i] == &me) {  // never itself (its own ends are 0 m away)
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
    // Measured from the end it moves toward (the front buffer, or the rear one when reversing).
    const trainz::sim::TrackPosition front = movingEnd(net, d);
    std::optional<StopAhead> best;
    auto consider = [&](double dist, const char* why) {
        if (dist >= 0 && (!best || dist < best->distance)) {
            best = StopAhead{dist, why};
        }
    };
    if (signals != nullptr) {
        if (const auto sg = signals->nextSignal(front, look); sg && signals->aspect(sg->index) == trainz::sim::Aspect::Stop) {
            consider(sg->distance, "signal");
        }
    }
    std::multimap<std::int32_t, double> taken;
    for (const auto& o : others) {
        taken.emplace(o.piece, o.offset);
    }
    double walked = 0;
    bool ended = true;
    net.walk(front, look, [&](const trainz::sim::TrackNetwork::Span& s) {
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
    if (d.destination && !d.destinationVia && trainCovers(net, d, *d.destination)) {
        // Its destination is under it already (it crept a little past a reversing point, or a
        // longer train stands over its mark): stop here. Otherwise, with the place no longer
        // ahead, it would set off and run on down the line.
        consider(margin, "destination (under the train)");
    } else if (d.destination && !d.destinationVia) {  // its destination (to stop at), if it lies ahead within `look`
        net.walk(front, look, [&](const trainz::sim::TrackNetwork::Span& sp) {
            if (sp.piece == d.destination->piece) {
                const double along = (d.destination->offset - sp.from) * sp.direction;
                if (along >= 0 && along <= std::fabs(sp.to - sp.from)) {
                    consider(sp.startDistance + along + margin, "destination");  // stop AT it, not short of it
                    return false;
                }
            }
            return true;
        });
    }
    return best;
}

void AiDriver::control(RouteTrain& d, const trainz::sim::TrackNetwork& net, const trainz::sim::SignalSystem* signals,
                       const std::vector<trainz::sim::Occupied>& others) const {
    const trainz::sim::TrackPosition front = movingEnd(net, d);  // distances from the end it moves toward
    auto& c = d.drive.controls;
    const double v = std::fabs(d.drive.speed);
    c.reverser = d.reversing ? -1 : +1;
    c.dynamicNotch = 0;
    // A Couple order's final approach (last leg, within 150 m): a calling-on move, so a red
    // signal neither stops nor holds it (see below).
    // Only when nothing else stands between: the target's own points begin at its end (the
    // destination), so any point of the others nearer than that belongs to another train (ChatGPT
    // review: proximity alone must not pass a red caused by an unrelated train).
    const bool callingOn = !d.coupleWith.empty() && d.destination && (d.legs.empty() || d.legIndex + 1 >= d.legs.size()) && [&] {
        const auto r = net.route(front, *d.destination, 160);
        if (!r || r->distance >= 150) {
            return false;
        }
        std::multimap<std::int32_t, double> taken;
        for (const auto& o : others) {
            taken.emplace(o.piece, o.offset);
        }
        bool between = false;
        net.walk(front, std::max(0.0, r->distance - 2.0), [&](const trainz::sim::TrackNetwork::Span& sp) {
            const auto [b, e] = taken.equal_range(sp.piece);
            const double lo = std::min(sp.from, sp.to), hi = std::max(sp.from, sp.to);
            for (auto it = b; it != e && !between; ++it) {
                between = it->second >= lo && it->second <= hi;
            }
            return !between;
        });
        return !between;
    }();
    if (callingOn) {
        d.heldRedSignal.reset();
    }
    if (d.heldRedSignal && signals != nullptr &&
        *d.heldRedSignal < signals->signals().size() &&
        signals->aspect(*d.heldRedSignal) != trainz::sim::Aspect::Stop) {
        d.heldRedSignal.reset();
    }
    double limit = cruise;
    if (signals != nullptr) {
        if (const auto sg = signals->nextSignal(front, 1500)) {
            const auto aspect = signals->aspect(sg->index);
            if (aspect == trainz::sim::Aspect::Caution) {
                limit = cruise / 2;
            } else if (aspect == trainz::sim::Aspect::Stop && sg->distance <= margin && !d.heldRedSignal && !callingOn) {
                // Remember before crossing: an overrun removes this signal from nextSignal.
                d.heldRedSignal = sg->index;
            }
        }
    }
    if (d.heldRedSignal || d.waitingForOrder) {  // at a red signal it reached, or its order says wait
        c.notch = 0, c.throttle = 0, c.brake = 1;
        return;
    }
    // Coupling: the last 40 m to the other train's end at walking pace, without stopping short
    // of it (its points would otherwise be a train ahead); signals still count.
    if (callingOn) {  // the last leg of a Couple order, nothing else between
        // Within 150 m it may also pass a red signal at walking pace (a calling-on move: the block
        // is usually red because the train it couples to stands in it). The port's rule.
        if (const auto r = net.route(front, *d.destination, 160); r && r->distance < 150) {
            if (r->distance >= 40) {  // still some way to go: a gentle approach at up to 10 km/h
                c.brake = v > 3.0 ? 0.6 : 0.0;
                c.notch = v < 2.5 ? 1 : 0;
                c.throttle = v < 2.5 ? 0.2 : 0.0;
                return;
            }
            c.brake = v > 1.2 ? 0.6 : 0.0;  // the last 40 m at walking pace, up to the buffers
            c.notch = v < 0.8 ? 1 : 0;
            c.throttle = v < 0.8 ? 0.15 : 0.0;
            return;
        }
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
        net.walk(front, stop->distance, [&](const trainz::sim::TrackNetwork::Span& s) {
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

std::map<std::string, trainz::sim::TrackPosition> routeTrackmarks(const trainz::sim::TrackNetwork& net,
                                                                  const std::vector<trainz::content::PlacedObject>& objects) {
    std::map<std::string, trainz::sim::TrackPosition> out;
    for (const auto& o : objects) {
        if (o.tag != "tsO2") {
            continue;
        }
        const auto a = trainz::content::trackAttachment(o);
        const auto len = a ? net.length(a->piece) : std::nullopt;
        const std::string name = signalName(o);
        if (!a || !len || name.empty() || out.count(name) != 0) {
            continue;
        }
        const double d = std::clamp(static_cast<double>(a->distance), 0.0, *len);
        out[name] = {a->piece, a->alongPiece ? d : *len - d, a->alongPiece ? +1 : -1};
    }
    return out;
}

std::vector<DriverOrders> driverOrders(const std::vector<trainz::content::Behaviour>& rules) {
    std::vector<DriverOrders> out;
    for (const auto& rule : rules) {
        if (rule.name != "Driver Setup") {
            continue;
        }
        const auto* drivers = rule.settings.root.child("drivers");
        if (drivers == nullptr || drivers->type != trainz::content::ConfigType::Group) {
            continue;
        }
        for (const auto& d : drivers->children) {
            const auto* vehicle = d.child("vehicle");
            const auto* cmds = d.child("driver-commands");
            if (vehicle == nullptr || vehicle->text.empty() || cmds == nullptr) {
                continue;
            }
            DriverOrders o;
            o.vehicle = vehicle->text;
            for (const auto& c : cmds->children) {
                const auto* k = c.child("command");
                const auto kuid = k != nullptr ? trainz::view::configKuid(*k) : std::nullopt;
                if (!kuid) {
                    continue;
                }
                DriverCommand dc{*kuid, {}, {}, {}};
                if (const auto* props = c.child("properties"); props != nullptr) {
                    for (const auto& p : props->children) {
                        if (!p.text.empty()) {
                            dc.properties[p.key] = p.text;
                        } else if (!p.ints.empty()) {
                            dc.properties[p.key] = std::to_string(p.ints[0]);
                        } else if (!p.floats.empty()) {
                            char b[32];
                            std::snprintf(b, sizeof b, "%g", static_cast<double>(p.floats[0]));
                            dc.properties[p.key] = b;
                        }
                    }
                }
                o.commands.push_back(std::move(dc));
            }
            out.push_back(std::move(o));
        }
    }
    return out;
}

bool releaseIfOnlySelfHeld(RouteTrain& t, const trainz::sim::SignalSystem& signals, const std::vector<trainz::sim::Occupied>& others) {
    if (!t.heldRedSignal || *t.heldRedSignal >= signals.signals().size() ||
        signals.aspect(*t.heldRedSignal) != trainz::sim::Aspect::Stop) {
        return false;
    }
    auto probe = signals;
    probe.update(others);
    if (probe.aspect(*t.heldRedSignal) == trainz::sim::Aspect::Stop) {
        return false;  // something else holds it red: stay
    }
    t.heldRedSignal.reset();
    return true;
}

std::string commandKind(const std::string& username) {
    std::string n = username;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (n == "navigate to trackmark") {
        return "navigate-to-trackmark";
    }
    if (n == "navigate to") {
        return "navigate-to-industry";
    }
    if (n == "navigate via trackmark") {
        return "navigate-via-trackmark";
    }
    if (n == "waitfor" || n == "wait for") {
        return "wait-for";
    }
    if (n == "waitfortrigger" || n == "wait for trigger") {
        return "wait-for-trigger";
    }
    if (n == "couple") {
        return "couple";
    }
    if (n == "load") {
        return "load";
    }
    if (n == "unload") {
        return "unload";
    }
    if (n == "uncouplez") {
        return "uncouple-engine";
    }
    if (n == "run around") {
        return "run-around";
    }
    if (n == "uncouplezfrom") {
        return "uncouple-from";
    }
    // Orders that change nothing about where trains go or what they carry: done at once.
    if (n == "hornz" || n == "horn" || n == "change destination sign" || n == "pantz") {
        return "no-effect";
    }
    return {};
}

void finishCouple(RouteTrain& t) {
    t.orderNote = "coupled to '" + t.coupleWith + "'";
    t.coupleWith.clear();
    t.reversing = false;
    t.legs.clear();
    t.destination.reset();
    ++t.orderIndex;
    t.orderClock = 0;
}

// Whether a place lies under the train (between its buffer ends).
bool trainCovers(const trainz::sim::TrackNetwork& net, const RouteTrain& t, const trainz::sim::TrackPosition& p) {
    bool covered = false;
    net.walk(trainRearEnd(net, t), std::max(0.0, t.length - 0.5), [&](const trainz::sim::TrackNetwork::Span& s) {
        covered = s.piece == p.piece && p.offset >= std::min(s.from, s.to) - 1e-6 && p.offset <= std::max(s.from, s.to) + 1e-6;
        return !covered;
    });
    return covered;
}

// Whether going from `from` along `r` (its junction settings applied on a copy of the network)
// meets `otherEnd` more than 1 m before the route's end: the way would run through the train.
bool passesThrough(const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& from,
                   const trainz::sim::TrackNetwork::Route& r, const trainz::sim::TrackPosition& otherEnd) {
    trainz::sim::TrackNetwork scratch = net;
    for (const auto& [vertex, piece] : r.switches) {
        scratch.switches[vertex] = piece;
    }
    bool met = false;
    scratch.walk(from, std::max(0.0, r.distance - 1.0), [&](const trainz::sim::TrackNetwork::Span& s) {
        if (s.piece == otherEnd.piece && otherEnd.offset >= std::min(s.from, s.to) - 1e-6 && otherEnd.offset <= std::max(s.from, s.to) + 1e-6) {
            met = true;
        }
        return !met;
    });
    return met;
}

namespace {
// Whether a way (its junction settings applied on a copy) meets any of `points` before its last metre.
bool wayBlocked(const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& from, const trainz::sim::TrackNetwork::Route& r,
                const std::vector<trainz::sim::Occupied>& points) {
    if (points.empty()) {
        return false;
    }
    trainz::sim::TrackNetwork scratch = net;
    for (const auto& [vertex, piece] : r.switches) {
        scratch.switches[vertex] = piece;
    }
    std::multimap<std::int32_t, double> taken;
    for (const auto& p : points) {
        taken.emplace(p.piece, p.offset);
    }
    bool met = false;
    scratch.walk(from, std::max(0.0, r.distance - 1.0), [&](const trainz::sim::TrackNetwork::Span& s) {
        const auto [b, e] = taken.equal_range(s.piece);
        const double lo = std::min(s.from, s.to), hi = std::max(s.from, s.to);
        for (auto it = b; it != e && !met; ++it) {
            met = it->second >= lo && it->second <= hi;
        }
        return !met;
    });
    return met;
}

// Walks a shunting plan as the train will drive it: each leg with its own junction settings (on a
// copy), from where the last one stopped (turned round after a reversal) up to 1 m short of the
// leg's end. `meets(piece, lo, hi)` is asked about each stretch of track passed; returns true as
// soon as it says yes, or when a leg's end is never reached (a plan that cannot be followed). (Working each leg out again as the shortest way could check a different
// path from the one the plan sets: an engine running round its cars was refused for the cars.)
bool walkLegs(const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& from,
              const std::vector<trainz::sim::TrackNetwork::Leg>& legs, const std::function<bool(std::int32_t, double, double)>& meets) {
    trainz::sim::TrackNetwork scratch = net;
    trainz::sim::TrackPosition at = from;
    for (const auto& leg : legs) {
        for (const auto& [vertex, piece] : leg.switches) {
            scratch.switches[vertex] = piece;
        }
        bool met = false, reached = false;
        scratch.walk(at, 20000, [&](const trainz::sim::TrackNetwork::Span& s) {
            double lo = std::min(s.from, s.to), hi = std::max(s.from, s.to);
            bool last = false;
            // The leg's end on this stretch, ahead of where the stretch starts: stop 1 m before it.
            if (s.piece == leg.end.piece && leg.end.offset >= lo - 1e-6 && leg.end.offset <= hi + 1e-6) {
                last = reached = true;
                if (std::fabs(leg.end.offset - s.from) < 1.0) {
                    return false;  // within the last metre already
                }
                const bool up = s.to >= s.from;
                (up ? hi : lo) = std::clamp(leg.end.offset - (up ? 1.0 : -1.0), lo, hi);
            }
            met = meets(s.piece, lo, hi);
            return !met && !last;
        });
        if (met || !reached) {
            return true;
        }
        at = leg.end;
        if (leg.reverseAfter) {
            at.direction = -at.direction;
        }
    }
    return false;
}

bool legsBlocked(const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& from,
                 const std::vector<trainz::sim::TrackNetwork::Leg>& legs, const std::vector<trainz::sim::Occupied>& points) {
    if (points.empty()) {
        return false;
    }
    std::multimap<std::int32_t, double> taken;
    for (const auto& p : points) {
        taken.emplace(p.piece, p.offset);
    }
    return walkLegs(net, from, legs, [&](std::int32_t piece, double lo, double hi) {
        const auto [b, e] = taken.equal_range(piece);
        return std::any_of(b, e, [&](const auto& p) { return p.second >= lo && p.second <= hi; });
    });
}
}  // namespace

bool shuntPassesThrough(const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& from,
                        const std::vector<trainz::sim::TrackNetwork::Leg>& legs, const trainz::sim::TrackPosition& otherEnd) {
    // The plan as it will be driven. (After a reversal the walk starts from the stop, turned
    // round: a little cautious, as it also covers the train's length.)
    return walkLegs(net, from, legs, [&](std::int32_t piece, double lo, double hi) {
        return piece == otherEnd.piece && otherEnd.offset >= lo - 1e-6 && otherEnd.offset <= hi + 1e-6;
    });
}

namespace {
// Plans a shunting way (with reversals) from either end of the train to `target`; on success sets
// the legs, the first leg's junctions, the direction and the destination (its end).
bool startShunt(RouteTrain& t, trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& target) {
    const double clearance = t.length + 15;
    trainz::sim::TrackPosition back = trainRearEnd(net, t);
    back.direction = -back.direction;
    // The shortest way; if another train stands in it, the shortest that keeps off the track
    // pieces where other trains stand (a way round them, e.g. an engine running round the cars
    // it has just left). Either way every leg is checked against the other trains.
    std::set<std::int32_t> taken;
    for (const auto& p : t.obstacles) {
        taken.insert(p.piece);
    }
    auto plan = [&](const trainz::sim::TrackPosition& from) -> std::optional<trainz::sim::TrackNetwork::ShuntRoute> {
        auto r = net.routeWithReversals(from, target, clearance);
        if (r && legsBlocked(net, from, r->legs, t.obstacles)) {
            r = taken.empty() ? std::nullopt : net.routeWithReversals(from, target, clearance, 20000, taken);
            if (r && legsBlocked(net, from, r->legs, t.obstacles)) {
                r.reset();  // it would still run into another train
            }
        }
        return r;
    };
    auto r = plan(trainFrontEnd(net, t));
    bool backing = false;
    if (auto rb = plan(back); rb && (!r || rb->distance < r->distance)) {
        r = rb, backing = true;
    }
    if (!r || r->legs.empty()) {
        return false;
    }
    t.legs = r->legs;
    t.legIndex = 0;
    t.reversing = backing;
    for (const auto& [vertex, piece] : t.legs[0].switches) {
        net.switches[vertex] = piece;
    }
    t.destination = t.legs[0].end;
    t.destinationVia = false;
    return true;
}

// On a shunting way: at the end of a leg (stopped close before it or over it), reverse and
// start the next. If the leg's end can no longer be reached (the plan no longer matches: a
// junction set otherwise, an overrun), plan the way again from here to the final place.
// Returns true while legs remain before the last one.
bool advanceShunt(RouteTrain& t, trainz::sim::TrackNetwork& net, double seconds) {
    if (t.legs.empty() || t.legIndex + 1 >= t.legs.size()) {
        return false;
    }
    const auto leg = t.legs[t.legIndex];
    const auto r = net.route(movingEnd(net, t), leg.end, 400);
    const bool stopped = std::fabs(t.drive.speed) < 0.1;
    if (!r && stopped && !trainCovers(net, t, leg.end)) {
        if (t.orderClock > 0) {  // the last new plan failed: stand, and try again in a while
            t.orderClock -= std::isfinite(seconds) && seconds > 0 ? seconds : 0;
            t.waitingForOrder = true;
            return true;
        }
        const auto finalPlace = t.legs.back().end;
        if (!startShunt(t, net, finalPlace)) {
            // No new plan (perhaps another train stands in the only way): hold on this leg and
            // try again in 10 s. Skipping the leg would set off with the old leg's destination
            // and direction (ChatGPT review).
            t.waitingForOrder = true;
            t.orderNote = "holding: no way on from here now";
            t.orderClock = 10;
            return true;
        }
        t.orderClock = 0;
        return t.legIndex + 1 < t.legs.size();
    }
    if (stopped && ((r && r->distance < 25) || trainCovers(net, t, leg.end))) {
        ++t.legIndex;
        t.reversing = !t.reversing;
        for (const auto& [vertex, piece] : t.legs[t.legIndex].switches) {
            net.switches[vertex] = piece;
        }
        t.destination = t.legs[t.legIndex].end;
    }
    return t.legIndex + 1 < t.legs.size();
}
}  // namespace

void runOrders(RouteTrain& t, trainz::sim::TrackNetwork& net, const std::map<std::string, trainz::sim::TrackPosition>& marks,
               const std::function<std::optional<trainz::sim::TrackPosition>(const std::string&)>& trainFront, double seconds,
               const std::function<TargetEnds(const std::string&)>& trainEnds, Industries* industries) {
    if (std::fabs(t.drive.speed) > 0.1) {
        t.lastMoved = t.drive.speed > 0 ? 1 : -1;
    }
    t.waitingForOrder = false;
    for (int guard = 0; guard < 64 && t.orderIndex < t.orders.size(); ++guard) {
        const DriverCommand& c = t.orders[t.orderIndex];
        auto prop = [&](const char* key) -> std::string {
            const auto it = c.properties.find(key);
            return it == c.properties.end() ? std::string() : it->second;
        };
        auto next = [&](const std::string& note) {
            ++t.orderIndex;
            t.orderClock = 0;
            t.destination.reset();
            t.reversing = false;
            t.legs.clear();
            t.orderNote = note;
        };
        if (c.kind == "navigate-to-trackmark" || c.kind == "navigate-via-trackmark" || c.kind == "navigate-to-industry") {
            const bool via = c.kind == "navigate-via-trackmark";
            const bool industry = c.kind == "navigate-to-industry";
            const auto mark = marks.find(industry ? industryKey(prop("industryname"), prop("trackname")) : prop("trackmarkname"));
            if (industry && mark == marks.end()) {  // not found on the track: hold (skipping it could change everything after)
                t.waitingForOrder = true;
                t.orderNote = "holding: no track found for industry '" + prop("industryname") + "' " + prop("trackname");
                return;
            }
            if (mark == marks.end()) {
                next("no trackmark '" + prop("trackmarkname") + "'");
                continue;
            }
            const std::string place = industry ? prop("industryname") + (prop("trackname").empty() ? "" : " " + prop("trackname")) : mark->first;
            if (!t.destination && t.orderClock > 0) {  // no way last time: try again in a while
                t.orderClock -= std::isfinite(seconds) && seconds > 0 ? seconds : 0;
                t.waitingForOrder = true;
                return;
            }
            if (!t.destination) {  // starting this order: the way forward, else backward
                auto ahead = net.route(trainFrontEnd(net, t), mark->second);
                if (ahead && wayBlocked(net, trainFrontEnd(net, t), *ahead, t.obstacles)) {
                    ahead.reset();  // it would run into another train
                }
                trainz::sim::TrackPosition back = trainRearEnd(net, t);
                back.direction = -back.direction;
                auto behind = ahead ? std::nullopt : net.route(back, mark->second);
                if (behind && wayBlocked(net, back, *behind, t.obstacles)) {
                    behind.reset();
                }
                if (!ahead && !behind) {
                    if (startShunt(t, net, mark->second)) {  // a way with reversals
                        t.destinationName = place;
                        t.orderNote = std::string(via ? "via '" : "to '") + place + "' (shunting, " +
                                      std::to_string(t.legs.size()) + " legs)";
                        return;
                    }
                    // No way now (perhaps another train stands in it): hold, and try again in 10 s.
                    t.waitingForOrder = true;
                    t.orderNote = "holding: no way to '" + place + "' now";
                    t.orderClock = 10;
                    return;
                }
                t.legs.clear();
                t.reversing = !ahead;
                const auto& r = ahead ? ahead : behind;
                for (const auto& [vertex, piece] : r->switches) {
                    net.switches[vertex] = piece;
                }
                t.destination = mark->second;
                t.destinationName = place;
                t.destinationVia = via;
                t.orderNote = std::string(via ? "via '" : "to '") + place + "'" + (t.reversing ? " (backing)" : "");
                return;
            }
            if (advanceShunt(t, net, seconds)) {
                return;  // still on an earlier leg
            }
            const auto r = net.route(movingEnd(net, t), mark->second);
            // Arrived: stopped close before it, or stopped with it under the train (a longer train,
            // or one backing, can stand over its mark).
            const bool stopped = std::fabs(t.drive.speed) < 0.1;
            const bool arrived = via ? (!r || r->distance < 5)
                                     : stopped && ((r && r->distance < 25) || trainCovers(net, t, mark->second));
            if (arrived) {
                t.atIndustry = industry ? prop("industryname") : std::string();
                next(std::string(via ? "passed '" : "reached '") + place + "'");
                continue;
            }
            // It cannot get there from here as planned (no way, or the way now runs into another
            // train): plan again.
            if (!via && stopped && (!r || wayBlocked(net, movingEnd(net, t), *r, t.obstacles))) {
                if (!startShunt(t, net, mark->second)) {
                    t.waitingForOrder = true;
                    t.orderNote = "holding: cannot reach '" + place + "' from here";
                }
                return;
            }
            return;  // on its way
        }
        if (c.kind == "couple") {
            const std::string vehicle = prop("vehiclename");
            if (t.coupleWith.empty() && t.orderClock > 0) {  // a failed attempt: try again in a while
                t.orderClock -= std::isfinite(seconds) && seconds > 0 ? seconds : 0;
                t.waitingForOrder = true;
                return;
            }
            if (t.coupleWith.empty()) {  // starting: the nearer end of that train, forward, backing or shunting
                const auto front = trainFrontEnd(net, t);
                trainz::sim::TrackPosition back = trainRearEnd(net, t);
                back.direction = -back.direction;
                const TargetEnds ends = trainEnds ? trainEnds(vehicle) : TargetEnds{};
                // The shortest direct way from either of our ends to an end of theirs that may be used
                // (the named vehicle's own); never to the other end instead: it holds (ChatGPT review).
                double best = 1e300;
                std::optional<trainz::sim::TrackNetwork::Route> way;
                std::optional<trainz::sim::TrackPosition> target;
                bool backing = false;
                for (std::size_t k = 0; k < ends.size(); ++k) {
                    if (!ends.allowed[k]) {
                        continue;
                    }
                    for (const bool b : {false, true}) {
                        const auto r = net.route(b ? back : front, ends[k]);
                        // Not through the train itself: its other end must not lie on the way (ChatGPT review).
                        // Nor into another train on the way: the intended train lies past the way's end (ChatGPT review).
                        if (r && r->distance < best && !(ends.size() == 2 && passesThrough(net, b ? back : front, *r, ends[1 - k])) &&
                            !wayBlocked(net, b ? back : front, *r, t.obstacles)) {
                            best = r->distance, way = r, target = ends[k], backing = b;
                        }
                    }
                }
                t.legs.clear();
                if (way) {
                    for (const auto& [vertex, piece] : way->switches) {
                        net.switches[vertex] = piece;
                    }
                    t.reversing = backing;
                    t.destination = *target;
                } else {
                    // A shunting way: plan one to each end and keep the shorter.
                    std::optional<RouteTrain> bestPlan;
                    double bestShunt = 1e300;
                    for (std::size_t k = 0; k < ends.size(); ++k) {
                        if (!ends.allowed[k]) {
                            continue;
                        }
                        const auto& e = ends[k];
                        RouteTrain trial = t;
                        trainz::sim::TrackNetwork scratch = net;  // startShunt sets junctions: on a copy first
                        // Every leg, not just a direct way, must keep clear of the train itself (ChatGPT review).
                        if (startShunt(trial, scratch, e) &&
                            !(ends.size() == 2 && shuntPassesThrough(net, movingEnd(net, trial), trial.legs, ends[1 - k]))) {
                            double total = 0;
                            trainz::sim::TrackPosition at = trial.reversing ? back : front;
                            for (const auto& leg : trial.legs) {
                                const auto r = scratch.route(at, leg.end, 20000);
                                total += r ? r->distance : 1e6;
                                at = leg.end;
                                at.direction = leg.reverseAfter ? -at.direction : at.direction;
                            }
                            if (total < bestShunt) {
                                bestShunt = total, bestPlan = trial, target = e;
                            }
                        }
                    }
                    if (!bestPlan || !startShunt(t, net, *target)) {
                        t.waitingForOrder = true;
                        t.orderNote = "holding: cannot reach '" + vehicle + "' to couple";
                        t.orderClock = 10;  // try again in 10 s, not every step
                        return;
                    }
                }
                t.coupleWith = vehicle;
                t.destinationVia = false;
                t.destinationName = vehicle;
                t.orderNote = "to couple to '" + vehicle + "'" +
                              (t.legs.empty() ? (t.reversing ? " (backing)" : "") : " (shunting, " + std::to_string(t.legs.size()) + " legs)");
            }
            advanceShunt(t, net, seconds);
            if (t.legs.empty()) {
                t.orderNote = "to couple to '" + vehicle + "'" + (t.reversing ? " (backing)" : "");
            }
            return;  // the caller couples on contact (finishCouple)
        }
        if (c.kind == "uncouple-engine") {
            if (std::fabs(t.drive.speed) > 0.1) {  // stop first
                t.waitingForOrder = true;
                t.orderNote = "stopping to uncouple";
                return;
            }
            auto parts = uncoupleAllButEngine(net, t);
            const bool any = !parts.empty();
            for (auto& p : parts) {
                t.leftBehind.push_back(std::move(p));
            }
            next(any ? "uncoupled the engine from its cars (a guess at 'UnCouplez')" : "uncouple: nothing to leave behind");
            return;  // the next order next step, once the cars left behind are among the obstacles
        }
        if (c.kind == "uncouple-from") {
            if (std::fabs(t.drive.speed) > 0.1) {
                t.waitingForOrder = true;
                t.orderNote = "stopping to uncouple";
                return;
            }
            long n = -1;
            try {
                n = std::stol(prop("timetowait"));
            } catch (const std::exception&) {
            }
            const auto g = engineGroup(t);
            // The train splits after vehicle n, counted from its trailing end (its front when it last
            // moved rear first, or never moved), and the engine keeps its side. Never part the engine
            // from its tender: such an order is not followed (the train holds).
            const auto size = static_cast<long>(t.cars.size());
            const long cut = t.lastMoved > 0 ? size - (n + 1) : n + 1;  // the second part's first car
            if (n < 0 || n > 100000 || (g && static_cast<long>(g->first) < cut && cut <= static_cast<long>(g->second))) {
                t.waitingForOrder = true;
                t.orderNote = "holding: cannot uncouple after vehicle " + std::to_string(n);
                return;
            }
            auto left = cut > 0 && cut < size
                            ? uncoupleFrom(net, t, static_cast<std::size_t>(cut), g && static_cast<long>(g->first) >= cut)
                            : std::nullopt;
            const bool any = left.has_value();
            if (left) {
                t.leftBehind.push_back(std::move(*left));
            }
            next(any ? "uncoupled after vehicle " + std::to_string(n) + " (a guess at 'UnCouplezFrom')"
                     : "uncouple: nothing after vehicle " + std::to_string(n));
            return;
        }
        if (c.kind == "load" || c.kind == "unload") {
            const bool load = c.kind == "load";
            t.waitingForOrder = true;
            const auto at = industries != nullptr ? industries->find(t.atIndustry) : Industries::iterator{};
            if (t.atIndustry.empty() || industries == nullptr || at == industries->end()) {
                t.orderNote = std::string("holding: no industry here to ") + c.kind + (load ? " from" : " at");
                return;
            }
            if (std::fabs(t.drive.speed) > 0.1) {
                t.orderNote = std::string("stopping to ") + c.kind;
                return;
            }
            const GoodsStep goods = load ? loadTrain(t, at->second) : unloadTrain(t, at->second);
            if (std::none_of(t.cars.begin(), t.cars.end(), [](const RouteTrain::Car& car) { return !car.cargo.empty(); })) {
                t.waitingForOrder = false;  // e.g. coaches whose configs have no queues
                next(std::string("nothing on this train carries goods: no ") + c.kind + "ing at '" + t.atIndustry + "'");
                continue;
            }
            if (!goods.possible) {
                t.orderNote = "holding: '" + t.atIndustry + (load ? "' has nothing for these cars" : "' takes nothing from these cars");
                return;
            }
            double units = 0;
            for (const auto& car : t.cars) {
                for (const auto& q : car.cargo) {
                    units += q.count;
                }
            }
            const std::string held = std::to_string(static_cast<long>(units)) + " units on board";
            if (goods.done) {
                t.waitingForOrder = false;
                next(std::string(load ? "loaded" : "unloaded") + " at '" + t.atIndustry + "', " + held);
                continue;
            }
            t.orderNote = std::string(load ? "loading" : "unloading") + " at '" + t.atIndustry + "', " + held;
            return;
        }
        if (c.kind == "run-around") {
            if (std::fabs(t.drive.speed) > 0.1) {
                t.waitingForOrder = true;
                t.orderNote = "stopping to run around";
                return;
            }
            const std::string far = runAroundCar(t);
            if (far.empty()) {
                t.waitingForOrder = true;
                t.orderNote = "holding: cannot run around (the engine is not at one end of its cars)";
                return;
            }
            for (auto& p : uncoupleAllButEngine(net, t)) {
                t.leftBehind.push_back(std::move(p));
            }
            // The rest of the order is a Couple to the far end; the order number stays the same.
            DriverCommand join = c;
            join.kind = "couple";
            join.properties = {{"vehiclename", far}};
            t.orders[t.orderIndex] = std::move(join);
            t.orderClock = 0;
            t.destination.reset();
            t.legs.clear();
            t.orderNote = "running around to '" + far + "'";
            return;  // next step, once the cars left behind are among the obstacles
        }
        if (c.kind == "no-effect") {
            next("passed over '" + c.name + "' (acknowledged, not performed)");
            continue;
        }
        if (c.kind == "wait-for") {
            t.waitingForOrder = true;
            double wait = 0;
            try {
                wait = std::stod(prop("timetowait"));
            } catch (const std::exception&) {
            }
            if (std::fabs(t.drive.speed) < 0.1 && std::isfinite(seconds) && seconds > 0) {
                t.orderClock += seconds;
            }
            t.orderNote = "waiting " + std::to_string(static_cast<int>(t.orderClock)) + " of " + std::to_string(static_cast<int>(wait)) + " s";
            if (t.orderClock >= wait) {
                t.waitingForOrder = false;
                next("waited " + std::to_string(static_cast<int>(wait)) + " s");
                continue;
            }
            return;
        }
        if (c.kind == "wait-for-trigger") {
            t.waitingForOrder = true;
            const auto trig = marks.find(prop("triggerid"));
            const auto other = trainFront ? trainFront(prop("trainid")) : std::nullopt;
            const auto tp = trig != marks.end() ? net.point(trig->second) : std::nullopt;
            const auto op = other ? net.point(*other) : std::nullopt;
            t.orderNote = "waiting for '" + prop("trainid") + "' at '" + prop("triggerid") + "'";
            if (trig == marks.end() || (tp && op && std::hypot(tp->x - op->x, tp->y - op->y) < 30)) {
                t.waitingForOrder = false;
                next(trig == marks.end() ? "no trigger '" + prop("triggerid") + "'" : "'" + prop("trainid") + "' reached '" + prop("triggerid") + "'");
                continue;
            }
            return;
        }
        // An order the port cannot follow yet: stop and hold here. Skipping it (a Load, a Couple)
        // would carry on with a plan that no longer matches the session (ChatGPT review).
        t.waitingForOrder = true;
        t.orderNote = "holding: order " + std::to_string(t.orderIndex + 1) + " '" + (c.name.empty() ? std::string("?") : c.name) +
                      "' is not followed yet";
        return;
    }
}

std::optional<std::pair<std::size_t, std::size_t>> engineGroup(const RouteTrain& t) {
    for (std::size_t i = 0; i < t.cars.size(); ++i) {
        if (!t.cars[i].spec.driven && !t.cars[i].engine) {
            continue;
        }
        std::size_t last = i;
        auto isTender = [&](std::size_t k) {
            std::string n = t.cars[k].name;
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return n.find("tender") != std::string::npos;
        };
        if (i + 1 < t.cars.size() && isTender(i + 1)) {
            last = i + 1;
        } else if (i > 0 && isTender(i - 1)) {  // a tender-first engine
            return std::pair{i - 1, i};
        }
        return std::pair{i, last};
    }
    return std::nullopt;
}

std::vector<RouteTrain> uncoupleAllButEngine(const trainz::sim::TrackNetwork& net, RouteTrain& t) {
    std::vector<RouteTrain> out;
    const auto g = engineGroup(t);
    if (!g || (g->first == 0 && g->second + 1 == t.cars.size())) {
        return out;
    }
    // Where each car's middle lies behind the lead (the first car's middle), along the train.
    auto lengthOf = [&](std::size_t k) { return t.cars[k].vehicle ? t.cars[k].vehicle->length : 0.0; };
    std::vector<double> middle(t.cars.size(), 0.0);
    for (std::size_t k = 1; k < t.cars.size(); ++k) {
        middle[k] = middle[k - 1] + 0.5 * lengthOf(k - 1) + (lengthOf(k - 1) > 0 && lengthOf(k) > 0 ? 0.5 : 0.0) + 0.5 * lengthOf(k);
    }
    auto part = [&](std::size_t from, std::size_t to) {  // cars [from, to] as a train placed where they stand
        RouteTrain p;
        p.cars.assign(t.cars.begin() + static_cast<std::ptrdiff_t>(from), t.cars.begin() + static_cast<std::ptrdiff_t>(to) + 1);
        p.vehicleCount = p.cars.size();
        p.index = t.index;
        p.drive.lead = t.drive.lead;
        net.advance(p.drive.lead, -middle[from]);
        refreshTrain(p);
        return p;
    };
    if (g->first > 0) {
        auto front = part(0, g->first - 1);
        front.parked = true;
        front.drive.controls.brake = 1;
        out.push_back(std::move(front));
    }
    if (g->second + 1 < t.cars.size()) {
        auto rear = part(g->second + 1, t.cars.size() - 1);
        rear.parked = true;
        rear.drive.controls.brake = 1;
        out.push_back(std::move(rear));
    }
    auto engine = part(g->first, g->second);
    t.cars = std::move(engine.cars);
    t.vehicleCount = t.cars.size();
    t.drive.lead = engine.drive.lead;
    t.drive.speed = 0;
    refreshTrain(t);
    return out;
}

std::optional<RouteTrain> uncoupleFrom(const trainz::sim::TrackNetwork& net, RouteTrain& t, std::size_t n, bool keepRear) {
    if (n == 0 || n >= t.cars.size()) {
        return std::nullopt;
    }
    // Where car n's middle lies behind the lead (the first car's middle), along the train.
    auto lengthOf = [&](std::size_t k) { return t.cars[k].vehicle ? t.cars[k].vehicle->length : 0.0; };
    double middle = 0;
    for (std::size_t k = 1; k <= n; ++k) {
        middle += 0.5 * lengthOf(k - 1) + (lengthOf(k - 1) > 0 && lengthOf(k) > 0 ? 0.5 : 0.0) + 0.5 * lengthOf(k);
    }
    const std::vector<RouteTrain::Car> first(t.cars.begin(), t.cars.begin() + static_cast<std::ptrdiff_t>(n));
    const std::vector<RouteTrain::Car> second(t.cars.begin() + static_cast<std::ptrdiff_t>(n), t.cars.end());
    trainz::sim::TrackPosition secondLead = t.drive.lead;
    net.advance(secondLead, -middle);
    RouteTrain left;
    left.cars = keepRear ? first : second;
    left.drive.lead = keepRear ? t.drive.lead : secondLead;
    left.vehicleCount = left.cars.size();
    left.index = t.index;
    left.parked = true;
    left.drive.controls.brake = 1;
    refreshTrain(left);
    t.cars = keepRear ? second : first;
    if (keepRear) {
        t.drive.lead = secondLead;
    }
    t.vehicleCount = t.cars.size();
    t.drive.speed = 0;
    refreshTrain(t);
    return left;
}

std::string runAroundCar(const RouteTrain& t) {
    const auto g = engineGroup(t);
    if (!g || t.cars.empty() || (g->first == 0) == (g->second + 1 == t.cars.size())) {
        return {};  // no engine, nothing but the engine, or cars on both sides of it
    }
    const std::string name = g->first == 0 ? t.cars.back().name : t.cars.front().name;
    const auto same = std::count_if(t.cars.begin(), t.cars.end(), [&](const RouteTrain::Car& c) { return c.name == name; });
    return same == 1 ? name : std::string();
}

Product assetProduct(trainz::view::AssetMeshes& meshes, const trainz::content::Kuid& kuid) {
    Product p;
    p.id = trainz::content::formatKuid(kuid);
    if (const auto cfg = meshes.config(kuid)) {
        if (const auto* k = cfg->child("kuid")) {
            if (const auto own = k->asKuid()) {
                p.id = trainz::content::formatKuid(*own);
            }
        }
        if (const auto* c = cfg->child("product-category")) {
            if (const auto k = c->asKuid()) {
                p.category = trainz::content::formatKuid(*k);
            }
        }
        if (const auto* m = cfg->child("mass"); m != nullptr && (!m->ints.empty() || !m->floats.empty())) {
            const double kg = m->ints.empty() ? static_cast<double>(m->floats[0]) : m->ints[0];
            p.unitKg = std::isfinite(kg) && kg > 0 ? kg : 0;
        }
    }
    return p;
}

namespace {
GoodsStep shift(RouteTrain& t, Industry& industry, bool load) {
    GoodsStep out;
    for (auto& car : t.cars) {
        for (auto& mine : car.cargo) {
            for (auto& theirs : industry.queues) {
                // Only a queue the industry makes goods in (or has some of and does not use) is loaded
                // from; only one that takes these goods is unloaded into.
                if (load && industry.empties(theirs)) {
                    continue;
                }
                if (!load && industry.fills(theirs)) {
                    continue;
                }
                CargoQueue& from = load ? theirs : mine;
                CargoQueue& to = load ? mine : theirs;
                out.possible = out.possible || to.takes(from.product);
                const double units = moveGoods(from, to);
                if (units > 0) {
                    out.moved += units;
                    car.spec.massKg = std::max(1000.0, car.spec.massKg + (load ? 1 : -1) * units * from.product.unitKg);
                }
                // Waiting helps while the industry still makes what this car takes, or uses up what it brings.
                // (passengers: whoever is there gets on or off, then the train goes).
                if (theirs.passengers) {
                    continue;
                }
                if (load ? mine.takes(theirs.product) && mine.room() > 0 && industry.fills(theirs)
                         : mine.count > 0 && theirs.takes(mine.product) && industry.empties(theirs)) {
                    out.done = false;
                }
            }
        }
    }
    if (out.moved > 0) {
        const double speed = t.drive.speed;
        refreshTrain(t);
        t.drive.speed = speed;
    }
    return out;
}
}  // namespace

GoodsStep loadTrain(RouteTrain& t, Industry& industry) { return shift(t, industry, true); }
GoodsStep unloadTrain(RouteTrain& t, Industry& industry) { return shift(t, industry, false); }

bool recordHasName(const trainz::content::PlacedObject& o, const std::string& name) {
    if (name.empty() || o.rest.size() < name.size() + 4) {
        return false;
    }
    for (std::size_t at = 0; at + 4 + name.size() <= o.rest.size(); ++at) {
        std::uint32_t n = 0;
        std::memcpy(&n, o.rest.data() + at, 4);
        if ((n == name.size() || n == name.size() + 1) && std::memcmp(o.rest.data() + at + 4, name.data(), name.size()) == 0 &&
            (n == name.size() || (at + 4 + name.size() < o.rest.size() && o.rest[at + 4 + name.size()] == 0))) {
            return true;
        }
    }
    return false;
}

std::string industryKey(const std::string& industry, const std::string& track) { return industry + "\n" + track; }

std::optional<IndustryTrack> industryTrack(const trainz::sim::TrackNetwork& net, const trainz::content::PlacedObject& o,
                                           const trainz::content::ConfigNode& config,
                                           const std::map<std::string, trainz::view::LocalFrame>& attachments, const std::string& track,
                                           double reach) {
    const auto* tracks = config.child("attached-track");
    if (tracks == nullptr || tracks->children.empty()) {
        return std::nullopt;
    }
    const auto* chosen = track.empty() ? &tracks->children.front() : tracks->child(track);
    const auto* vertices = chosen != nullptr ? chosen->child("vertices") : nullptr;
    if (vertices == nullptr) {
        return std::nullopt;
    }
    IndustryTrack out;
    out.track = chosen->key;
    const double c = std::cos(o.yaw), s = std::sin(o.yaw);
    std::vector<trainz::sim::TrackPosition> found;
    for (const auto& v : vertices->children) {
        ++out.points;
        const auto at = attachments.find(v.text);
        if (at == attachments.end()) {
            continue;
        }
        const double lx = static_cast<double>(at->second.t[0]), ly = static_cast<double>(at->second.t[1]);
        const double x = o.worldX() + c * lx - s * ly, y = o.worldY() + s * lx + c * ly;
        const auto p = net.locate(x, y, reach);
        const auto w = p ? net.point(*p) : std::nullopt;
        if (!w) {
            continue;
        }
        out.worst = std::max(out.worst, std::hypot(w->x - x, w->y - y));
        found.push_back(*p);
    }
    out.found = found.size();
    if (found.empty()) {
        return std::nullopt;
    }
    out.at = found[found.size() / 2];
    return out;
}

std::optional<ShuntBlock> shuntBlock(const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& from,
                                     const std::vector<trainz::sim::TrackNetwork::Leg>& legs, const std::vector<trainz::sim::Occupied>& points) {
    trainz::sim::TrackNetwork scratch = net;
    trainz::sim::TrackPosition at = from;
    for (std::size_t k = 0; k < legs.size(); ++k) {
        const auto& leg = legs[k];
        for (const auto& [vertex, piece] : leg.switches) {
            scratch.switches[vertex] = piece;
        }
        std::optional<ShuntBlock> found;
        bool reached = false;
        scratch.walk(at, 20000, [&](const trainz::sim::TrackNetwork::Span& sp) {
            const double length = std::fabs(sp.to - sp.from);
            double stop = length;  // along this stretch
            if (sp.piece == leg.end.piece) {
                const double along = (leg.end.offset - sp.from) * sp.direction;
                if (along >= -1e-6 && along <= length + 1e-6) {
                    reached = true;
                    stop = along - 1.0;  // the last metre is free, as in the check itself
                }
            }
            for (const auto& o : points) {
                const double along = o.piece == sp.piece ? (o.offset - sp.from) * sp.direction : -1.0;
                if (along >= 0 && along <= stop && (!found || sp.startDistance + along < found->distance)) {
                    found = ShuntBlock{k, sp.startDistance + along, o};
                }
            }
            return !found && !reached;
        });
        if (found) {
            return found;
        }
        if (!reached) {
            return ShuntBlock{k, 0, std::nullopt};
        }
        at = leg.end;
        if (leg.reverseAfter) {
            at.direction = -at.direction;
        }
    }
    return std::nullopt;
}

std::size_t parkUndriven(std::vector<RouteTrain>& trains) {
    const bool session = std::any_of(trains.begin(), trains.end(), [](const RouteTrain& t) { return !t.orders.empty(); });
    std::size_t n = 0;
    for (auto& t : trains) {
        if (session ? t.orders.empty() : !engineGroup(t)) {
            t.parked = true;
            t.drive.controls.throttle = 0;
            t.drive.controls.notch = 0;
            t.drive.controls.brake = 1;
            ++n;
        }
    }
    return n;
}

bool carriesVehicle(const RouteTrain& t, const std::string& vehicle) {
    return std::any_of(t.cars.begin(), t.cars.end(), [&](const RouteTrain::Car& c) { return c.name == vehicle; });
}

bool coupleEndAllowed(const RouteTrain& other, const std::string& vehicle, bool theirFront) {
    if (!carriesVehicle(other, vehicle)) {
        return false;
    }
    const bool first = other.cars.front().name == vehicle, last = other.cars.back().name == vehicle;
    return first == last || (theirFront ? first : last);  // in the middle, or the only car: either end
}

TargetEnds vehicleTrainEnds(const trainz::sim::TrackNetwork& net, const std::vector<RouteTrain>& trains, const RouteTrain* me,
                            const std::string& vehicle) {
    for (const auto& o : trains) {
        if (&o != me && !o.cars.empty() && carriesVehicle(o, vehicle)) {
            TargetEnds out({trainFrontEnd(net, o), trainRearEnd(net, o)});
            out.allowed = {coupleEndAllowed(o, vehicle, true), coupleEndAllowed(o, vehicle, false)};
            return out;
        }
    }
    return {};
}

void refuseCouple(RouteTrain& t) {
    t.orderNote = "holding: met '" + t.coupleWith + "' at the wrong end";
    t.coupleWith.clear();
    t.reversing = false;
    t.legs.clear();
    t.destination.reset();
    t.drive.speed = 0;
    t.orderClock = 10;
    t.waitingForOrder = true;
}

}  // namespace trainz::game
