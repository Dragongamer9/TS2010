// The own game's generated world and driving. No game files are read.
#include "game/Drive.h"
#include "game/GeneratedWorld.h"
#include "game/RouteDrive.h"

#include <gtest/gtest.h>

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
