// The own game's generated world and driving. No game files are read.
#include "game/Drive.h"
#include "game/GeneratedWorld.h"
#include "game/RouteDrive.h"
#include "game/TrainSound.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string>

using namespace trainz;

TEST(GeneratedWorld, TheOvalIsClosedAndAsLongAsDesigned) {
    const auto w = game::generateWorld(1);
    const sim::TrackNetwork net(w.track);
    // 2 x 600 m straights and two 150 m half circles (the bends are curves through vertices).
    const double designed = 2 * 600 + 2 * std::numbers::pi * 150;
    sim::TrackPosition p{w.startPiece, 0, +1};
    const auto start = net.point(p);
    ASSERT_TRUE(start);
    double lap = 0;
    for (int i = 0; i < 3000 && !(lap > 100 && std::hypot(net.point(p)->x - start->x, net.point(p)->y - start->y) < 2.5); ++i) {
        const auto m = net.advance(p, 2.0);
        ASSERT_FALSE(m.stoppedAtEnd);
        lap += m.moved;
    }
    EXPECT_NEAR(lap, designed, 0.01 * designed);  // within 1%: the curve model is near a circle
    // Back where it began (on the ring's last piece or the first: the same place).
    EXPECT_LT(std::hypot(net.point(p)->x - start->x, net.point(p)->y - start->y), 2.5);
    EXPECT_TRUE(p.piece == w.startPiece || p.piece == w.startPiece + 26) << p.piece;
}

TEST(GeneratedWorld, TheTurnoutLeadsToABufferedSiding) {
    const auto w = game::generateWorld(1);
    sim::TrackNetwork net(w.track);
    EXPECT_EQ(net.exits(w.junctionVertex, w.startPiece).size(), 2U);
    net.switches[w.junctionVertex] = w.sidingPiece;
    sim::TrackPosition p{w.startPiece, 150, +1};
    const auto m = net.advance(p, 1000);
    EXPECT_TRUE(m.stoppedAtEnd);  // the buffer at the siding's end
    const auto end = net.point(p);
    ASSERT_TRUE(end);
    EXPECT_NEAR(end->x, 150, 1e-6);
    EXPECT_NEAR(end->y, -162, 1e-6);
}

TEST(GeneratedWorld, TerrainIsFlatUnderTheTrackAndHillyAway) {
    const auto w = game::generateWorld(1);
    EXPECT_NEAR(w.heightAt(0, -150), -0.4, 0.05);  // just below the rails
    EXPECT_NEAR(w.heightAt(450, 0), -0.4, 0.05);   // on the east bend (x 300 + 150)
    double lo = 1e9, hi = -1e9;
    for (double x = -500; x <= 500; x += 50) {
        lo = std::min(lo, w.heightAt(x, 300)), hi = std::max(hi, w.heightAt(x, 300));
    }
    EXPECT_GT(hi - lo, 5);  // hills away from the track
    EXPECT_DOUBLE_EQ(w.heightAt(1e6, 0), 0);  // outside the grid
    EXPECT_NE(game::generateWorld(2).heightAt(0, 300), w.heightAt(0, 300));  // the seed changes the hills
}

TEST(Drive, AcceleratesAlongTheTrackAndStopsAtTheBuffer) {
    const auto w = game::generateWorld(1);
    sim::TrackNetwork net(w.track);
    game::Drive d;
    d.lead = {w.startPiece, 150, +1};
    d.spec = sim::makeTrain({sim::electricLocomotive(), sim::freightWagon(60), sim::freightWagon(60)});
    d.controls.throttle = 1;
    const auto before = net.point(d.lead);
    for (int i = 0; i < 30; ++i) {
        d.step(net, 1.0);
    }
    EXPECT_GT(d.speed, 5);
    EXPECT_GT(net.point(d.lead)->x, before->x + 50);  // went east along the south straight
    net.switches[w.junctionVertex] = w.sidingPiece;  // too late: already past the turnout
    sim::TrackNetwork siding(w.track);
    siding.switches[w.junctionVertex] = w.sidingPiece;
    game::Drive s = d;
    s.lead = {w.startPiece, 150, +1};
    s.speed = 0;
    bool hit = false;
    for (int i = 0; i < 300 && !hit; ++i) {
        s.step(siding, 1.0);
        hit = s.atBuffer;
    }
    EXPECT_TRUE(hit);
    EXPECT_DOUBLE_EQ(s.speed, 0);
}

TEST(Drive, TheConsistIsLaidBehindTheLead) {
    const auto w = game::generateWorld(1);
    const sim::TrackNetwork net(w.track);
    const auto loco = game::boxLocomotive(), wagon = game::boxWagon({1, 2, 3}), bogie = game::boxBogie();
    const std::vector<game::ConsistVehicle> consist = {{&loco, 18}, {&wagon, 14}};
    view::Scene s;
    const sim::TrackPosition lead{w.startPiece, 300, +1};
    EXPECT_EQ(game::addConsist(s, net, lead, consist, bogie), 2);
    EXPECT_EQ(s.triangles.size(), loco.triangles.size() + wagon.triangles.size() + 4 * bogie.triangles.size());
    // Every corner of the train is behind the lead (the south straight runs +x from x -300).
    const double leadX = net.point(lead)->x;
    double minX = 1e9, maxX = -1e9;
    for (const auto& t : s.triangles) {
        for (const auto& p : t.p) {
            minX = std::min(minX, p.x), maxX = std::max(maxX, p.x);
        }
    }
    EXPECT_LE(maxX, leadX + 0.01);
    EXPECT_NEAR(minX, leadX - (18 + 1 + 14), 1.0);  // both vehicles and the 1 m gap
    // The models are the game's own boxes, each with two bogie points.
    EXPECT_EQ(loco.attachments.size(), 2U);
    EXPECT_FALSE(loco.placeholder);
}

TEST(RouteDrive, LocateTrainStartAndFacing) {
    const auto w = game::generateWorld(1);
    const sim::TrackNetwork net(w.track);
    const auto at = net.locate(0, -149);  // 1 m off the south straight, x 0
    ASSERT_TRUE(at);
    EXPECT_EQ(at->piece, w.startPiece + 1);  // the turnout is at x -100
    EXPECT_NEAR(net.point(*at)->x, 0, 1e-6);
    EXPECT_FALSE(net.locate(0, 0));  // the middle of the oval: no track within 5 m
    // A train: vehicles stored front to back (here the front is at x 0, the next at x -15).
    content::Train t;
    auto vehicle = [](double x, double y) {
        content::Vehicle v;
        v.boardX = static_cast<std::int16_t>(std::floor(x / content::kBaseboardSize));
        v.boardY = static_cast<std::int16_t>(std::floor(y / content::kBaseboardSize));
        v.x = static_cast<float>(x - v.boardX * content::kBaseboardSize);
        v.y = static_cast<float>(y - v.boardY * content::kBaseboardSize);
        return v;
    };
    t.vehicles = {vehicle(0, -150), vehicle(-15, -150)};
    const auto s = game::trainStart(net, t);
    ASSERT_TRUE(s);
    EXPECT_NEAR(std::cos(net.point(*s)->heading), 1, 1e-6);  // runs east, away from the second
    t.vehicles = {vehicle(-15, -150), vehicle(0, -150)};
    const auto r = game::trainStart(net, t);
    ASSERT_TRUE(r);
    EXPECT_NEAR(std::cos(net.point(*r)->heading), -1, 1e-6);  // runs west
    t.vehicles = {vehicle(0, 0)};
    EXPECT_FALSE(game::trainStart(net, t));
    t.vehicles.clear();
    EXPECT_FALSE(game::trainStart(net, t));
}

TEST(RouteDrive, VehiclesFromTheirModelsOnTheirBogies) {
    const auto w = game::generateWorld(1);
    const sim::TrackNetwork net(w.track);
    const auto wagon = game::boxWagon({1, 2, 3}), bogie = game::boxBogie();
    const auto v = game::routeVehicle(wagon, &bogie, &bogie);
    EXPECT_NEAR(v.length, 14, 1e-4);  // the model's extent along y
    EXPECT_TRUE(v.onBogies);
    EXPECT_NEAR(v.bogieY[0], -14.0 / 3, 1e-4);
    EXPECT_NEAR(v.bogieY[1], 14.0 / 3, 1e-4);
    view::PreparedMesh plain = wagon;
    plain.attachments.clear();
    EXPECT_FALSE(game::routeVehicle(plain, nullptr, nullptr).onBogies);
    view::Scene s;
    const sim::TrackPosition front{w.startPiece + 1, 200, +1};
    EXPECT_EQ(game::addRouteTrain(s, net, front, {v, v, game::routeVehicle(plain, nullptr, nullptr)}), 3);
    EXPECT_EQ(s.triangles.size(), 3 * wagon.triangles.size() + 4 * bogie.triangles.size());  // 2 wagons with 2 bogies each
    // On straight track the first body is centred on `front` (its bogie points' middle).
    const double fx = net.point(front)->x;
    double lo = 1e9, hi = -1e9;
    for (std::size_t i = 0; i < wagon.triangles.size(); ++i) {
        for (const auto& p : s.triangles[i].p) {
            lo = std::min(lo, p.x), hi = std::max(hi, p.x);
        }
    }
    EXPECT_NEAR(0.5 * (lo + hi), fx, 0.01);
}

TEST(RouteDrive, VehicleSpecFromItsOwnData) {
    trainz::content::EngineSpec loco;
    loco.axleCount = 6;
    loco.throttlePower = {{}, {{0, 165}, {13, 0}}, {{0, 330}, {22, 0}}};
    const auto l = trainz::game::ownVehicleSpec(190'508.0, &loco);
    EXPECT_DOUBLE_EQ(l.massKg, 190'508);
    EXPECT_TRUE(l.driven);
    ASSERT_EQ(l.tractiveCurveN.size(), 2u);
    EXPECT_DOUBLE_EQ(l.tractiveCurveN.front().second, 330'000);  // the top notch, kN -> N
    EXPECT_DOUBLE_EQ(l.davisA, 6.5 * 190.508 + 80 * 6);            // RP-548 with its own axles
    ASSERT_EQ(l.notchCurvesN.size(), 3u);  // every notch, kN -> N
    EXPECT_TRUE(l.notchCurvesN[0].empty());
    EXPECT_DOUBLE_EQ(l.notchCurvesN[1].front().second, 165'000);

    trainz::content::EngineSpec wagon;  // a wagon's enginespec: motor only
    const auto w = trainz::game::ownVehicleSpec(30'000.0, &wagon);
    EXPECT_FALSE(w.driven);
    EXPECT_DOUBLE_EQ(w.massKg, 30'000);
    for (const auto bad : {std::optional<double>{}, std::optional<double>{-5.0}, std::optional<double>{1e9},
                           std::optional<double>{std::numeric_limits<double>::quiet_NaN()}}) {
        const auto f = trainz::game::ownVehicleSpec(bad, nullptr);
        EXPECT_DOUBLE_EQ(f.massKg, 40'000);
        EXPECT_FALSE(f.driven);
        EXPECT_NE(f.source.find("no usable mass"), std::string::npos);
    }
}

TEST(RouteDrive, SignalsTurnToTheirTrackAndLightTheirLamps) {
    // One straight piece along +x, 200 m; a signal 50 m along it for travel toward -x.
    trainz::content::TrackLayout layout;
    trainz::content::TrackVertex a, b;
    a.index = 0, b.index = 1, b.x = 200;
    a.links = {{1, 0}}, b.links = {{1, 0}};
    layout.vertices = {{0, a}, {1, b}};
    trainz::content::TrackStretch st;
    st.index = 1, st.from = 0, st.to = 1, st.rest = {0x20, 0, 0, 0};
    layout.stretches = {st};
    const trainz::sim::TrackNetwork net(layout);
    trainz::content::PlacedObject o;
    o.tag = "sgOb";
    o.x = 150, o.y = 2.5;
    o.rest = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    const std::uint8_t tail[23] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0x48, 0x42, 0, 0, 0, 0, 0, 0};  // piece 1, 50 m, dir 0
    o.rest.insert(o.rest.end(), tail, tail + 23);
    std::vector<trainz::content::PlacedObject> objects{o};
    EXPECT_EQ(trainz::game::orientSignals(net, objects), 1u);
    // Travel toward -x: heading pi, so yaw = pi/2 (model +y along -x).
    EXPECT_NEAR(std::remainder(objects[0].yaw - std::numbers::pi / 2, 2 * std::numbers::pi), 0, 1e-5);
    const auto sigs = trainz::game::routeSignals(net, objects);
    ASSERT_EQ(sigs.size(), 1u);
    EXPECT_EQ(sigs[0].object, 0);
    EXPECT_NEAR(sigs[0].at.offset, 150, 1e-6);  // 50 m from the `to` end
    EXPECT_EQ(sigs[0].at.direction, -1);

    trainz::view::PreparedMesh mesh;
    trainz::view::LocalFrame lamp;
    lamp.t[0] = 0, lamp.t[1] = -0.2f, lamp.t[2] = 4;
    mesh.attachments["a.light2"] = lamp;
    trainz::content::SignalLook look;
    look.states = {{1, {2}}, {8, {0}}};
    look.coronas = {{2, "corona_red.tga"}};
    const auto lit = trainz::game::signalLamps(objects[0], mesh, look, 0);
    ASSERT_EQ(lit.size(), 1u);
    // Model -y turned by yaw pi/2 points toward +x: the lamp sits 0.2 m toward +x, facing trains coming from +x.
    EXPECT_NEAR(lit[0].x, 150.2, 1e-4);
    EXPECT_NEAR(lit[0].y, 2.5, 1e-4);
    EXPECT_NEAR(lit[0].z, 4, 1e-6);
    EXPECT_EQ(lit[0].rgb[0], 255);
    EXPECT_TRUE(trainz::game::signalLamps(objects[0], mesh, look, 2).empty());  // a.light0 not on the model
    trainz::view::PreparedMesh old;  // no `signals` list: the older named points
    old.attachments["a.green"] = lamp;
    const auto green = trainz::game::signalLamps(objects[0], old, trainz::content::SignalLook{}, 2);
    ASSERT_EQ(green.size(), 1u);
    EXPECT_GT(green[0].rgb[1], green[0].rgb[0]);
    EXPECT_TRUE(trainz::game::signalLamps(objects[0], old, trainz::content::SignalLook{}, 0).empty());  // no a.red
}

TEST(Drive, BrakeHandleAppliesLapsAndReleases) {
    trainz::game::BrakeHandle h;
    double b = 0;
    h.position = trainz::game::BrakeHandle::Position::Application;
    h.step(b, 2);
    EXPECT_NEAR(b, 2 * trainz::game::BrakeHandle::kApplyPerSecond, 1e-12);
    h.position = trainz::game::BrakeHandle::Position::Lap;
    h.step(b, 5);
    EXPECT_NEAR(b, 0.3, 1e-12);  // held
    h.position = trainz::game::BrakeHandle::Position::Release;
    h.step(b, 100);
    EXPECT_EQ(b, 0);  // never below 0
    h.position = trainz::game::BrakeHandle::Position::Emergency;
    h.step(b, 0.01);
    EXPECT_EQ(b, 1);
    EXPECT_STREQ(h.name(), "emergency");
    b = std::numeric_limits<double>::quiet_NaN();
    h.position = trainz::game::BrakeHandle::Position::Lap;
    h.step(b, std::numeric_limits<double>::infinity());
    EXPECT_EQ(b, 0);
}

namespace {
// A straight 2 km line along +x in two 1 km pieces, and an AI train at its start.
struct AiLine {
    trainz::content::TrackLayout layout;
    AiLine() {
        trainz::content::TrackVertex a, b, c;
        a.index = 0, b.index = 1, c.index = 2;
        b.x = 1000, c.x = 2000;
        a.links = {{1, 0}}, b.links = {{1, 0}, {2, 1}}, c.links = {{2, 0}};
        layout.vertices = {{0, a}, {1, b}, {2, c}};
        trainz::content::TrackStretch s1, s2;
        s1.index = 1, s1.from = 0, s1.to = 1, s1.rest = {0x20, 0, 0, 0};
        s2.index = 2, s2.from = 1, s2.to = 2, s2.rest = {0x20, 0, 0, 0};
        layout.stretches = {s1, s2};
    }
};
trainz::game::RouteTrain aiTrain(double offset) {
    trainz::game::RouteTrain t;
    t.drive.lead = {1, offset, +1};
    t.drive.spec = trainz::sim::makeTrain({trainz::sim::electricLocomotive(), trainz::sim::freightWagon(40)});
    t.length = 30;
    return t;
}
// Runs the driver for `seconds`; returns the furthest distance travelled from the start offset.
double run(trainz::game::RouteTrain& t, const trainz::sim::TrackNetwork& net, const trainz::sim::SignalSystem* sig,
           const std::vector<trainz::sim::Occupied>& others, double seconds, double& topSpeed) {
    const trainz::game::AiDriver ai;
    topSpeed = 0;
    for (double s = 0; s < seconds; s += 0.5) {
        ai.control(t, net, sig, others);
        t.drive.step(net, 0.5);
        topSpeed = std::max(topSpeed, t.drive.speed);
    }
    return 0;
}
double along(const trainz::sim::TrackPosition& p) { return (p.piece == 1 ? 0 : 1000) + p.offset; }
}  // namespace

TEST(AiDriver, CruisesThenStopsShortOfTheLineEnd) {
    const AiLine line;
    const trainz::sim::TrackNetwork net(line.layout);
    auto t = aiTrain(10);
    double top = 0;
    run(t, net, nullptr, {}, 900, top);
    EXPECT_NEAR(top, trainz::game::AiDriver{}.cruise, 1.0);  // reaches about its cruise speed
    EXPECT_NEAR(t.drive.speed, 0, 1e-9);                     // and stands
    EXPECT_FALSE(t.drive.atBuffer);                          // never ran into the end
    EXPECT_GT(along(t.drive.lead), 2000 - trainz::game::AiDriver{}.margin - 15);
    EXPECT_LT(along(t.drive.lead), 2000 - 1);
}

TEST(AiDriver, StopsBeforeASignalAtStopAndATrainAhead) {
    const AiLine line;
    const trainz::sim::TrackNetwork net(line.layout);
    // A signal at 1,200 m whose block holds a standing train at 1,500 m.
    trainz::sim::SignalSystem sig(net, {{{2, 200, +1}, "S"}});
    const std::vector<trainz::sim::Occupied> standing{{2, 500}};
    sig.update(standing);
    ASSERT_EQ(sig.aspect(0), trainz::sim::Aspect::Stop);
    auto t = aiTrain(10);
    double top = 0;
    run(t, net, &sig, standing, 600, top);
    EXPECT_NEAR(t.drive.speed, 0, 1e-9);
    EXPECT_LT(along(t.drive.lead), 1200);                  // short of the signal
    EXPECT_GT(along(t.drive.lead), 1200 - 20 - 15);        // within its margin and a little
    // The signal clears: it runs on, and stops short of the standing train.
    sig.update({});
    run(t, net, &sig, standing, 600, top);
    EXPECT_NEAR(t.drive.speed, 0, 1e-9);
    EXPECT_GT(along(t.drive.lead), 1200);
    EXPECT_LT(along(t.drive.lead), 1500);
    const auto why = trainz::game::AiDriver{}.stopAhead(t, net, &sig, standing);
    ASSERT_TRUE(why.has_value());
    EXPECT_STREQ(why->why, "train");
}

TEST(AiDriver, StopsOnDownhillWithActualBrakeForce) {
    AiLine line;
    line.layout.vertices.at(1).z = -30;
    line.layout.vertices.at(2).z = -60;
    const trainz::sim::TrackNetwork net(line.layout);
    auto t = aiTrain(10);
    t.drive.spec.maxBrakeForceN = 0.52 * t.drive.spec.massKg;
    t.drive.speed = trainz::game::AiDriver{}.cruise;
    const trainz::game::AiDriver ai;
    for (int i = 0; i < 1800; ++i) {
        ai.control(t, net, nullptr, {});
        t.drive.step(net, 0.5);
        ASSERT_FALSE(t.drive.atBuffer);
        ASSERT_LT(along(t.drive.lead), 1980);
    }
    EXPECT_NEAR(t.drive.speed, 0, 1e-9);
    EXPECT_GT(along(t.drive.lead), 1965);
}

TEST(AiDriver, StopsWithLowGrip) {
    const AiLine line;
    const trainz::sim::TrackNetwork net(line.layout);
    auto t = aiTrain(10);
    t.drive.spec.adhesion = 0.02;
    t.drive.speed = trainz::game::AiDriver{}.cruise;
    const trainz::game::AiDriver ai;
    for (int i = 0; i < 1800; ++i) {
        ai.control(t, net, nullptr, {});
        t.drive.step(net, 0.5);
        ASSERT_FALSE(t.drive.atBuffer);
        ASSERT_LT(along(t.drive.lead), 1980);
    }
    EXPECT_NEAR(t.drive.speed, 0, 1e-9);
    EXPECT_GT(along(t.drive.lead), 1965);
}

TEST(AiDriver, HoldsAfterStartingFiveMetresBeforeRedUntilItClears) {
    const AiLine line;
    const trainz::sim::TrackNetwork net(line.layout);
    trainz::sim::SignalSystem sig(net, {{{1, 200, +1}, "S"}});
    const trainz::game::AiDriver ai;
    for (const double initialSpeed : {1.0, 4.0}) {
        SCOPED_TRACE(initialSpeed);
        sig.update({{1, 500}});
        auto t = aiTrain(195);
        t.drive.spec.maxBrakeForceN = 0.52 * t.drive.spec.massKg;
        t.drive.speed = initialSpeed;
        for (int i = 0; i < 120; ++i) {
            ai.control(t, net, &sig, {});
            ASSERT_EQ(t.drive.controls.brake, 1);
            ASSERT_EQ(t.drive.controls.throttle, 0);
            ASSERT_EQ(t.drive.controls.notch, 0);
            t.drive.step(net, 0.5);
        }
        ASSERT_TRUE(t.heldRedSignal.has_value());
        EXPECT_EQ(*t.heldRedSignal, 0u);
        EXPECT_NEAR(t.drive.speed, 0, 1e-9);
        if (initialSpeed == 1.0) {
            EXPECT_LT(t.drive.lead.offset, 200);  // enough room to stop before red
        } else {
            EXPECT_GT(t.drive.lead.offset, 200);  // unavoidable overrun must not release the brake
        }
        const double stoppedAt = t.drive.lead.offset;
        sig.update({});
        ai.control(t, net, &sig, {});
        EXPECT_FALSE(t.heldRedSignal.has_value());
        EXPECT_EQ(t.drive.controls.brake, 0);
        t.drive.step(net, 0.5);
        EXPECT_GT(t.drive.lead.offset, stoppedAt);
    }
}

// At cruise speed with a stop far ahead (the line end 1.2 km away), the driver must not brake:
// the full-brake test compares the speed with the stopping envelope, not the cruise-capped limit.
TEST(AiDriver, CruisingTowardAFarStopDoesNotBrake) {
    const AiLine line;
    const trainz::sim::TrackNetwork net(line.layout);
    auto t = aiTrain(800);
    t.drive.speed = trainz::game::AiDriver{}.cruise;
    const auto stop = trainz::game::AiDriver{}.stopAhead(t, net, nullptr, {});
    ASSERT_TRUE(stop.has_value());
    EXPECT_STREQ(stop->why, "line end");
    trainz::game::AiDriver{}.control(t, net, nullptr, {});
    EXPECT_EQ(t.drive.controls.brake, 0);
}

namespace {
// A train of `n` 10 m cars (a dummy model), its front at `offset` on piece 1 of AiLine, facing `dir`.
trainz::game::RouteTrain carTrain(int n, double offset, int dir, int firstId) {
    static const trainz::view::PreparedMesh mesh;
    trainz::game::RouteTrain t;
    for (int k = 0; k < n; ++k) {
        trainz::game::RouteVehicle v;
        v.body = &mesh;
        v.length = 10;
        trainz::sim::VehicleSpec spec = trainz::sim::freightWagon(20);
        spec.name = std::to_string(firstId + k);  // to follow each car
        t.cars.push_back({spec, v});
    }
    t.vehicleCount = static_cast<std::size_t>(n);
    t.drive.lead = {1, offset, dir};
    trainz::game::refreshTrain(t);
    return t;
}
std::string order(const trainz::game::RouteTrain& t) {
    std::string s;
    for (const auto& c : t.cars) {
        s += c.spec.name + (c.vehicle && c.vehicle->flipped ? "f" : "") + " ";
    }
    return s;
}
}  // namespace

TEST(Coupling, FindsTheTouchingEndAndJoinsInOneLine) {
    const AiLine line;
    const trainz::sim::TrackNetwork net(line.layout);
    // Us: 2 cars of 10 m, first middle (the lead) at 500 facing +x: front end 505, rear end 484.5.
    auto me = carTrain(2, 500, +1, 1);
    EXPECT_NEAR(me.length, 21, 1e-9);
    EXPECT_NEAR(trainz::game::trainFrontEnd(net, me).offset, 505, 1e-9);
    EXPECT_NEAR(trainz::game::trainRearEnd(net, me).offset, 484.5, 1e-9);
    // (a) Their rear touches our front: they face our way; their rear end 505.3 (lead 531.3).
    {
        std::vector<trainz::game::RouteTrain> others{carTrain(3, 531.3, +1, 10)};
        const auto c = trainz::game::findContact(net, me, others, 1.0);
        ASSERT_TRUE(c.has_value());
        EXPECT_TRUE(c->atOurFront);
        EXPECT_FALSE(c->theirFront);
        EXPECT_NEAR(c->gap, 0.3, 1e-6);
        auto m = me;
        trainz::game::coupleTrains(net, m, others[0], *c);
        EXPECT_EQ(order(m), "10 11 12 1 2 ");
        EXPECT_NEAR(m.drive.lead.offset, 531.3, 1e-6);  // their first middle is the new lead
        EXPECT_EQ(m.drive.lead.direction, +1);
        EXPECT_TRUE(others[0].cars.empty());
        EXPECT_NEAR(m.drive.spec.massKg, 5 * 20'000, 1e-6);
    }
    // (b) Their front touches our front: they face us (lead 510.4 facing -x, front end 505.4);
    // turned round, the new lead is their last car's middle (531.4), facing away from us.
    {
        std::vector<trainz::game::RouteTrain> others{carTrain(3, 510.4, -1, 10)};
        const auto c = trainz::game::findContact(net, me, others, 1.0);
        ASSERT_TRUE(c.has_value());
        EXPECT_TRUE(c->atOurFront);
        EXPECT_TRUE(c->theirFront);
        EXPECT_NEAR(c->gap, 0.4, 1e-6);
        auto m = me;
        trainz::game::coupleTrains(net, m, others[0], *c);
        EXPECT_EQ(order(m), "12f 11f 10f 1 2 ");
        EXPECT_NEAR(m.drive.lead.offset, 531.4, 1e-6);
        EXPECT_EQ(m.drive.lead.direction, +1);
    }
    // (c) Their front touches our rear: behind us, facing our way (lead 479.1, front end 484.1);
    // they join behind us in their own order.
    {
        std::vector<trainz::game::RouteTrain> others{carTrain(3, 479.1, +1, 10)};
        const auto c = trainz::game::findContact(net, me, others, 1.0, false);
        ASSERT_TRUE(c.has_value());
        EXPECT_FALSE(c->atOurFront);
        EXPECT_TRUE(c->theirFront);
        auto m = me;
        trainz::game::coupleTrains(net, m, others[0], *c);
        EXPECT_EQ(order(m), "1 2 10 11 12 ");
        EXPECT_NEAR(m.drive.lead.offset, 500, 1e-6);
    }
    // Nothing within reach: no contact.
    std::vector<trainz::game::RouteTrain> far{carTrain(3, 700, +1, 10)};
    EXPECT_FALSE(trainz::game::findContact(net, me, far, 1.0).has_value());
}

TEST(Coupling, UncouplingLeavesTheRearCarStandingBehind) {
    const AiLine line;
    const trainz::sim::TrackNetwork net(line.layout);
    auto me = carTrain(3, 500, +1, 1);
    me.drive.speed = 0;
    const auto rest = trainz::game::uncoupleRear(net, me);
    ASSERT_TRUE(rest.has_value());
    EXPECT_EQ(order(me), "1 2 ");
    EXPECT_EQ(order(*rest), "3 ");
    EXPECT_TRUE(rest->parked);
    EXPECT_NEAR(rest->drive.lead.offset, 484.5 - 0.5 - 5, 1e-6);  // its middle, just behind our new rear end
    EXPECT_NEAR(me.drive.spec.massKg, 40'000, 1e-6);
    auto one = carTrain(1, 100, +1, 1);
    EXPECT_FALSE(trainz::game::uncoupleRear(net, one).has_value());  // the last car stays
}

TEST(Coupling, DetachedWagonsKeepTheirRealPhysicsWithoutFallbackPower) {
    const AiLine line;
    const trainz::sim::TrackNetwork net(line.layout);
    auto me = carTrain(3, 500, +1, 1);
    const auto wagon = me.cars.back().spec;
    me.fallbackPower = true;
    trainz::game::refreshTrain(me);
    EXPECT_GT(me.drive.spec.maxTractiveForceN, 0);
    EXPECT_DOUBLE_EQ(me.drive.spec.maxBrakeForceN, 3 * wagon.brakeForceN);
    const auto rest = trainz::game::uncoupleRear(net, me);
    ASSERT_TRUE(rest.has_value());
    EXPECT_FALSE(rest->fallbackPower);
    EXPECT_DOUBLE_EQ(rest->drive.spec.maxTractiveForceN, 0);
    EXPECT_DOUBLE_EQ(rest->drive.spec.maxPowerW, 0);
    EXPECT_DOUBLE_EQ(rest->drive.spec.maxBrakeForceN, wagon.brakeForceN);
    EXPECT_DOUBLE_EQ(rest->drive.spec.massKg, wagon.massKg);
    EXPECT_FALSE(rest->cars.front().spec.driven);
    EXPECT_DOUBLE_EQ(me.cars.front().spec.tractiveForceN, 0);
}

TEST(Coupling, ContactAtOppositeEndCannotHideMovingEnd) {
    const AiLine line;
    const trainz::sim::TrackNetwork net(line.layout);
    const auto me = carTrain(2, 500, +1, 1);
    // Our buffers: 505 and 484.5. Rear obstruction is nearer, but we move forward.
    const std::vector<trainz::game::RouteTrain> others{
        carTrain(1, 479.4, +1, 10), carTrain(1, 510.4, +1, 20)};
    const auto front = trainz::game::findContact(net, me, others, 1, true);
    ASSERT_TRUE(front.has_value());
    EXPECT_TRUE(front->atOurFront);
    EXPECT_EQ(front->other, 1u);
    EXPECT_NEAR(front->gap, 0.4, 1e-6);
    const auto rear = trainz::game::findContact(net, me, others, 1, false);
    ASSERT_TRUE(rear.has_value());
    EXPECT_FALSE(rear->atOurFront);
    EXPECT_EQ(rear->other, 0u);
    EXPECT_NEAR(rear->gap, 0.1, 1e-6);
}

TEST(Coupling, OccupancyCoversBufferEndsAndSignalUnderFrontHalf) {
    const AiLine line;
    const trainz::sim::TrackNetwork net(line.layout);
    for (const int direction : {+1, -1}) {
        SCOPED_TRACE(direction);
        const auto me = carTrain(2, 500, direction, 1);
        const auto front = trainz::game::trainFrontEnd(net, me);
        const auto rear = trainz::game::trainRearEnd(net, me);
        const auto taken = trainz::game::trainOccupies(net, me, 1);
        ASSERT_FALSE(taken.empty());
        EXPECT_NEAR(taken.front().offset, front.offset, 1e-6);
        EXPECT_NEAR(taken.back().offset, rear.offset, 1e-6);
        for (const auto& point : taken) {
            EXPECT_GE(point.offset, std::min(front.offset, rear.offset) - 1e-6);
            EXPECT_LE(point.offset, std::max(front.offset, rear.offset) + 1e-6);
        }
        trainz::sim::SignalSystem signals(net, {{{1, 500 + direction * 2.0, direction}, "front half"}});
        signals.update(taken);
        EXPECT_EQ(signals.aspect(0), trainz::sim::Aspect::Stop);
    }
}

TEST(Coupling, TurnedRoundVehiclesAreDrawnTheOtherWay) {
    const AiLine line;
    const trainz::sim::TrackNetwork net(line.layout);
    trainz::view::PreparedMesh body;
    trainz::view::LocalTriangle tri{};
    tri.p[0][1] = 5;  // a point at the model's front (+y)
    body.triangles.push_back(tri);
    trainz::game::RouteVehicle v;
    v.body = &body;
    v.length = 10;
    trainz::view::Scene a, b;
    trainz::game::addRouteTrain(a, net, {1, 500, +1}, {v});
    v.flipped = true;
    trainz::game::addRouteTrain(b, net, {1, 500, +1}, {v});
    ASSERT_EQ(a.triangles.size(), 1u);
    ASSERT_EQ(b.triangles.size(), 1u);
    // The vehicle's middle is at the lead (500): its front point 5 m along +x, or turned round 5 m back.
    EXPECT_NEAR(a.triangles[0].p[0].x, 505, 1e-6);
    EXPECT_NEAR(b.triangles[0].p[0].x, 495, 1e-6);
}

TEST(AirBrake, PipeFollowsTheHandleAndTheBrakesFollowThePipe) {
    using P = trainz::game::BrakeHandle::Position;
    trainz::game::AirBrake b;
    b.setPressures(0.0070, 0.0066, 0.0045);  // charged, initial, full service
    EXPECT_DOUBLE_EQ(b.pipe, 0.0070);
    EXPECT_DOUBLE_EQ(b.demand(), 0);
    // Application: the initial reduction at once on the equaliser, the pipe catching up.
    b.step(P::Application, 0.01, 1);
    EXPECT_NEAR(b.equaliser, 0.0066 - 0.0025 / 7 * 0.01, 1e-9);
    double d = 0;
    for (int i = 0; i < 40; ++i) {  // 20 s: well past full service
        d = b.step(P::Application, 0.5, 1);
    }
    EXPECT_NEAR(b.equaliser, 0.0045, 1e-12);
    EXPECT_NEAR(d, 1.0, 1e-3);
    // Release, then lap halfway: it holds.
    for (int i = 0; i < 20; ++i) {
        b.step(P::Release, 0.5, 1);
    }
    const double held = b.step(P::Lap, 0.5, 1);
    EXPECT_GT(held, 0.1);
    EXPECT_LT(held, 0.9);
    for (int i = 0; i < 20; ++i) {
        b.step(P::Lap, 0.5, 1);
    }
    EXPECT_NEAR(b.demand(), b.step(P::Lap, 0.5, 1), 0.02);  // the pipe settles on the equaliser and stays
    // Emergency vents the pipe at once: full brake.
    EXPECT_DOUBLE_EQ(b.step(P::Emergency, 0.1, 1), 1.0);
    EXPECT_DOUBLE_EQ(b.pipe, 0);
}

TEST(AirBrake, LongerTrainsRespondSlowerAndBadPressuresAreIgnored) {
    using P = trainz::game::BrakeHandle::Position;
    trainz::game::AirBrake shortTrain, longTrain;
    shortTrain.setPressures(0.0070, 0.0066, 0.0045);
    longTrain.setPressures(0.0070, 0.0066, 0.0045);
    for (int i = 0; i < 4; ++i) {
        shortTrain.step(P::Application, 0.5, 2);
        longTrain.step(P::Application, 0.5, 60);
    }
    EXPECT_GT(shortTrain.demand(), longTrain.demand());
    trainz::game::AirBrake bad;
    bad.setPressures(0.004, 0.005, 0.006);  // full above charged: nonsense, defaults kept
    EXPECT_DOUBLE_EQ(bad.charged, 0.00736041);
    bad.setPressures(std::nullopt, std::nullopt, 0.004);
    EXPECT_DOUBLE_EQ(bad.full, 0.0044992);
    trainz::game::AirBrake noInitial;
    noInitial.setPressures(0.0070, std::nullopt, 0.0045);  // initial: 15% of the way down
    EXPECT_NEAR(noInitial.initial, 0.0070 - 0.15 * 0.0025, 1e-12);
    double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(std::isfinite(noInitial.step(P::Release, nan, 1)));
}

namespace {
trainz::game::SoundClip constantClip(float value, std::size_t n) {
    trainz::game::SoundClip c;
    c.samples.assign(n, value);
    return c;
}
float mixedLevel(trainz::game::TrainSound& s, std::size_t n) {
    std::vector<float> out(n, 0.0f);
    s.mix(out.data(), n, 1.0f);
    return out.back();
}
}  // namespace

TEST(TrainSound, NotchChangesStepThroughTheirClipsThenLoop) {
    trainz::game::TrainSound s;
    s.addEngineClip("idle 1.wav", constantClip(0.1f, 100));
    s.addEngineClip("idle 2.wav", constantClip(0.2f, 100));
    s.addEngineClip("idle 3.wav", constantClip(0.3f, 100));
    s.addEngineClip("up 1 - 2.wav", constantClip(0.5f, 50));
    s.addEngineClip("up 2 - 3.wav", constantClip(0.6f, 50));
    s.addEngineClip("down 3 - 2.wav", constantClip(0.7f, 50));
    ASSERT_TRUE(s.hasEngine());
    s.setNotch(0);
    EXPECT_FLOAT_EQ(mixedLevel(s, 30), 0.1f);  // notch 0 plays idle 1
    EXPECT_EQ(s.playing(), "idle 1.wav");
    s.setNotch(3);
    EXPECT_FLOAT_EQ(mixedLevel(s, 10), 0.5f);  // at once: the 1 -> 2 clip
    EXPECT_EQ(s.playing(), "up 1 - 2.wav");
    EXPECT_FLOAT_EQ(mixedLevel(s, 50), 0.6f);  // then 2 -> 3
    EXPECT_FLOAT_EQ(mixedLevel(s, 60), 0.3f);  // then the notch 3 loop
    EXPECT_FLOAT_EQ(mixedLevel(s, 250), 0.3f);  // and it loops
    s.setNotch(2);
    EXPECT_FLOAT_EQ(mixedLevel(s, 10), 0.7f);  // down 3 -> 2
    EXPECT_FLOAT_EQ(mixedLevel(s, 60), 0.2f);
    // A step without its clip goes straight to the next loop.
    s.addEngineClip("idle 4.wav", constantClip(0.4f, 100));
    s.setNotch(4);  // up 2 - 3 exists, up 3 - 4 does not
    mixedLevel(s, 60);
    EXPECT_FLOAT_EQ(mixedLevel(s, 10), 0.4f);
}

TEST(TrainSound, HornStartsLoopsWhileHeldAndEnds) {
    trainz::game::TrainSound s;
    s.addHornClip("horn_start.wav", constantClip(0.25f, 40));
    s.addHornClip("horn_loop.wav", constantClip(0.5f, 40));
    s.addHornClip("horn_end.wav", constantClip(0.75f, 40));
    ASSERT_TRUE(s.hasHorn());
    EXPECT_FLOAT_EQ(mixedLevel(s, 10), 0.0f);  // silent: no engine, horn off
    s.setHorn(true);
    EXPECT_FLOAT_EQ(mixedLevel(s, 10), 0.25f);
    EXPECT_FLOAT_EQ(mixedLevel(s, 40), 0.5f);   // looping
    EXPECT_FLOAT_EQ(mixedLevel(s, 200), 0.5f);
    s.setHorn(false);
    EXPECT_FLOAT_EQ(mixedLevel(s, 10), 0.75f);  // the end clip
    EXPECT_FLOAT_EQ(mixedLevel(s, 40), 0.0f);   // then silence
}

TEST(TrainSound, ClipsAreMixedToMonoAndResampled) {
    trainz::content::PcmSound pcm;
    pcm.channels = 2;
    pcm.sampleRate = 44100;
    for (int i = 0; i < 4410; ++i) {  // 0.1 s: left 0.5, right -0.25
        pcm.samples.push_back(16384);
        pcm.samples.push_back(-8192);
    }
    const auto c = trainz::game::toClip(pcm, 22050);
    EXPECT_NEAR(static_cast<double>(c.samples.size()), 2205, 1);
    EXPECT_NEAR(c.samples[100], 0.125f, 1e-4);
    trainz::content::PcmSound empty;
    EXPECT_TRUE(trainz::game::toClip(empty, 22050).samples.empty());
}

// No time passing changes nothing, even with the handle in application or emergency (review).
TEST(AirBrake, NoElapsedTimeChangesNothing) {
    using P = trainz::game::BrakeHandle::Position;
    trainz::game::AirBrake b;
    b.setPressures(0.0070, 0.0066, 0.0045);
    for (const double t : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        for (const P h : {P::Application, P::Emergency, P::Release, P::Lap}) {
            EXPECT_DOUBLE_EQ(b.step(h, t, 10), 0.0);
            EXPECT_DOUBLE_EQ(b.equaliser, 0.0070);
            EXPECT_DOUBLE_EQ(b.pipe, 0.0070);
        }
    }
}

TEST(RunningSounds, ClacksAtEveryJointPointsAndBrakeHiss) {
    trainz::game::TrainSound s;
    // Every clip a 10-sample burst of its own level, so the mix shows what played and when.
    s.addEffect("sounds/track/wheel_clang1.wav", constantClip(0.5f, 10));
    s.addEffect("sounds/track/wheel_clang2.wav", constantClip(0.5f, 10));
    s.addEffect("sounds/track/wheel_clang3.wav", constantClip(0.5f, 10));
    s.addEffect("sounds/track/wheel_clang4.wav", constantClip(0.5f, 10));
    s.addEffect("sounds/junction/wheels_over_points_1.wav", constantClip(0.25f, 10));
    s.addEffect("sounds/airbrakes/start.wav", constantClip(0.1f, 10));
    s.addEffect("sounds/airbrakes/idle 1.wav", constantClip(0.1f, 10));
    s.addEffect("sounds/airbrakes/stop.wav", constantClip(0.1f, 10));
    trainz::game::RunningSounds r;
    // 25 m/s for 1 s: 25 m, joints at 12 m and 24 m -> two pairs of clacks.
    r.update(s, 25, 25, 1.0, false, false);
    std::vector<float> out(trainz::game::TrainSound::kRate * 6 / 5, 0.0f);  // 1.2 s: the last axle's clack is at 1.056 s
    s.mix(out.data(), out.size(), 1.0f);
    int bursts = 0;
    for (std::size_t i = 1; i < out.size(); ++i) {
        bursts += out[i] > 0.01f && out[i - 1] <= 0.01f ? 1 : 0;
    }
    EXPECT_EQ(bursts, 4);  // 2 joints x 2 axles
    // Where the first clack falls: 12 m of 25 m into the second.
    std::size_t first = 0;
    while (first < out.size() && out[first] <= 0.01f) {
        ++first;
    }
    EXPECT_NEAR(static_cast<double>(first), 12.0 / 25.0 * trainz::game::TrainSound::kRate, 2);
    EXPECT_NEAR(r.sinceJoint, 1.0, 1e-9);  // 1 m past the last joint
    // Standing still: no clacks. Over points: its clip. The brake pipe falling: the hiss.
    r.update(s, 0, 0, 1.0, true, true);
    std::vector<float> more(100, 0.0f);
    s.mix(more.data(), more.size(), 1.0f);
    EXPECT_NEAR(more[0], 0.25f * 0.3f + 0.1f * 0.5f + 0.1f * 0.4f, 1e-5);  // points, start, loop
    EXPECT_TRUE(r.hissing);
    r.update(s, 0, 0, 1.0, false, false);
    EXPECT_FALSE(r.hissing);
}

TEST(RunningSounds, FlangeSquealOnTightCurvesAtSpeedAndTheBell) {
    trainz::game::TrainSound s;
    s.addEffect("sounds/track/flange_squeal.wav", constantClip(0.5f, 100));
    trainz::game::RunningSounds r;
    auto level = [&](double speed, double radius) {
        r.update(s, speed, 0, 0.5, false, false, radius);
        std::vector<float> out(10, 0.0f);
        s.mix(out.data(), out.size(), 1.0f);
        return out[0];
    };
    EXPECT_FLOAT_EQ(level(20, std::numeric_limits<double>::infinity()), 0.0f);  // straight: none
    EXPECT_FLOAT_EQ(level(20, 400), 0.0f);                                     // gentle curve: none
    EXPECT_GT(level(20, 150), 0.0f);                                           // tight: squeal
    EXPECT_GT(level(20, 80), level(20, 150));                                  // tighter: louder
    EXPECT_FLOAT_EQ(level(1, 80), 0.0f);                                       // crawling: none
    // The bell loops while on.
    trainz::game::TrainSound b;
    b.addHornClip("w_bell_1.wav", constantClip(0.5f, 40));
    ASSERT_TRUE(b.hasBell());
    b.setBell(true);
    std::vector<float> out(100, 0.0f);
    b.mix(out.data(), out.size(), 1.0f);
    EXPECT_FLOAT_EQ(out[90], 0.25f);  // 0.5 at the bell's 0.5 volume, still going after one pass
    b.setBell(false);
    std::vector<float> quiet(10, 0.0f);
    b.mix(quiet.data(), quiet.size(), 1.0f);
    EXPECT_FLOAT_EQ(quiet[0], 0.0f);
}
