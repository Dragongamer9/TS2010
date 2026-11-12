// The game's own simulation core: driving along track and train physics. No game files.
#include "sim/TrackNetwork.h"
#include "sim/TrainPhysics.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

using namespace trainz::content;
using namespace trainz::sim;

namespace {

struct Layout {
    TrackLayout t;
    void vertex(std::int32_t index, float x, float y, std::vector<TrackLink> links, float z = 0) {
        TrackVertex v;
        v.index = index, v.x = x, v.y = y, v.z = z, v.links = std::move(links);
        t.vertices.emplace(index, v);
    }
    void piece(std::int32_t index, std::int32_t from, std::int32_t to, std::uint8_t flags = 0x20) {
        TrackStretch s;
        s.index = index, s.from = from, s.to = to, s.rest = {flags, 0, 0, 0};
        t.stretches.push_back(s);
    }
};

// 0 --A-- 1 --B-- 2 along +x (100 m each); B is stored backwards (from 2 to 1).
Layout line() {
    Layout l;
    l.vertex(0, 0, 0, {{1, 0}});
    l.vertex(1, 100, 0, {{1, 0}, {2, 1}});
    l.vertex(2, 200, 0, {{2, 0}});
    l.piece(1, 0, 1);
    l.piece(2, 2, 1);
    return l;
}

}  // namespace

TEST(TrackNetwork, LengthsAndPoints) {
    const auto l = line();
    const TrackNetwork n(l.t);
    ASSERT_TRUE(n.length(1));
    EXPECT_NEAR(*n.length(1), 100, 1e-6);
    EXPECT_FALSE(n.length(99));
    const auto p = n.point({1, 25, +1});
    ASSERT_TRUE(p);
    EXPECT_NEAR(p->x, 25, 1e-6);
    EXPECT_NEAR(p->heading, 0, 1e-6);
    const auto back = n.point({1, 25, -1});
    EXPECT_NEAR(std::fabs(back->heading), std::numbers::pi, 1e-6);  // facing the other way
}

TEST(TrackNetwork, DrivesOnThroughAVertexIntoAReversedPiece) {
    const auto l = line();
    const TrackNetwork n(l.t);
    TrackPosition p{1, 0, +1};
    const auto m = n.advance(p, 150);
    EXPECT_NEAR(m.moved, 150, 1e-6);
    EXPECT_FALSE(m.stoppedAtEnd);
    EXPECT_EQ(p.piece, 2);
    EXPECT_NEAR(p.offset, 50, 1e-6);  // B runs from x 200 to 100: 50 m from its `from` end
    const auto pt = n.point(p);
    ASSERT_TRUE(pt);
    EXPECT_NEAR(pt->x, 150, 1e-6);
    EXPECT_NEAR(pt->heading, 0, 1e-6);  // still heading +x
    // And back again.
    const auto r = n.advance(p, -120);
    EXPECT_NEAR(r.moved, -120, 1e-6);
    EXPECT_EQ(p.piece, 1);
    EXPECT_NEAR(n.point(p)->x, 30, 1e-6);
}

TEST(TrackNetwork, LineEndsStopTheTrain) {
    const auto l = line();
    const TrackNetwork n(l.t);
    TrackPosition p{1, 80, +1};
    const auto m = n.advance(p, 500);
    EXPECT_TRUE(m.stoppedAtEnd);
    EXPECT_NEAR(m.moved, 120, 1e-6);  // 20 m on A, 100 m on B
    EXPECT_NEAR(n.point(p)->x, 200, 1e-6);
    const auto back = n.advance(p, -1000);
    EXPECT_TRUE(back.stoppedAtEnd);
    EXPECT_NEAR(n.point(p)->x, 0, 1e-6);
    TrackPosition nowhere{77, 0, 1};
    EXPECT_DOUBLE_EQ(n.advance(nowhere, 10).moved, 0);
    EXPECT_DOUBLE_EQ(n.advance(p, std::numeric_limits<double>::quiet_NaN()).moved, 0);
}

TEST(TrackNetwork, JunctionsTakeTheStraightWayUnlessSwitched) {
    // 0 --A-- 1, then on vertex 1's far side: B straight on to (200, 0), C off to (190, 40).
    Layout l;
    l.vertex(0, 0, 0, {{1, 0}});
    l.vertex(1, 100, 0, {{1, 0}, {2, 1}, {3, 1}});
    l.vertex(2, 200, 0, {{2, 0}});
    l.vertex(3, 190, 40, {{3, 0}});
    l.piece(1, 0, 1);
    l.piece(2, 1, 2);
    l.piece(3, 1, 3);
    TrackNetwork n(l.t);
    EXPECT_EQ(n.exits(1, 1), (std::vector<std::int32_t>{2, 3}));
    TrackPosition p{1, 90, +1};
    n.advance(p, 20);
    EXPECT_EQ(p.piece, 2);  // straight on
    n.switches[1] = 3;
    TrackPosition q{1, 90, +1};
    n.advance(q, 20);
    EXPECT_EQ(q.piece, 3);  // the switch set to the branch
    n.switches[1] = 99;     // not a way out of this vertex: ignored
    TrackPosition r{1, 90, +1};
    n.advance(r, 20);
    EXPECT_EQ(r.piece, 2);
}

TEST(TrackNetwork, GradeAlongTheDirectionOfTravel) {
    Layout l;
    l.vertex(0, 0, 0, {{1, 0}}, 0);
    l.vertex(1, 100, 0, {{1, 0}}, 2);  // 2% up toward vertex 1
    l.piece(1, 0, 1);
    const TrackNetwork n(l.t);
    const double rise = 2 / std::hypot(100.0, 2.0);  // per metre travelled along the slope
    EXPECT_NEAR(n.point({1, 50, +1})->grade, rise, 1e-9);
    EXPECT_NEAR(n.point({1, 50, -1})->grade, -rise, 1e-9);
    EXPECT_NEAR(*n.length(1), std::hypot(100.0, 2.0), 1e-6);
}

TEST(TrainPhysics, AcceleratesToABalanceAndBrakesToAStop) {
    const TrainSpec spec;
    double v = 0;
    Controls full{1, 0, +1};
    double distance = 0;
    for (int i = 0; i < 3600; ++i) {  // an hour: the last metres per second come slowly
        distance += stepTrain(v, spec, full, 0, 1.0);
    }
    EXPECT_GT(v, 40);  // ~150 km/h with these numbers
    // Balance: power / v equals the resistance.
    const double resist = spec.davisA + spec.davisB * v + spec.davisC * v * v;
    EXPECT_NEAR(spec.maxPowerW / v, resist, resist * 0.02);
    EXPECT_GT(distance, 10000);
    // Full brake: stops, and does not roll backwards.
    Controls stop{0, 1, +1};
    for (int i = 0; i < 300; ++i) {
        stepTrain(v, spec, stop, 0, 1.0);
    }
    EXPECT_DOUBLE_EQ(v, 0);
    // Reverser backwards: moves the other way.
    double w = 0;
    Controls back{1, 0, -1};
    const double d = stepTrain(w, spec, back, 0, 5.0);
    EXPECT_LT(w, 0);
    EXPECT_LT(d, 0);
}

TEST(TrainPhysics, GradesBrakesAndBadInput) {
    const TrainSpec spec;
    double v = 0;
    Controls idle{0, 0, 0};
    for (int i = 0; i < 30; ++i) {
        stepTrain(v, spec, idle, 0.02, 1.0);  // 2% up the way it faces, no brakes
    }
    EXPECT_LT(v, 0);  // rolls back down
    double held = 0;
    Controls brakes{0, 1, 0};
    for (int i = 0; i < 30; ++i) {
        stepTrain(held, spec, brakes, 0.02, 1.0);
    }
    EXPECT_DOUBLE_EQ(held, 0);  // brakes hold it on the grade
    double flat = 0;
    stepTrain(flat, spec, idle, 0, 10);
    EXPECT_DOUBLE_EQ(flat, 0);  // nothing moves a train at rest on the level
    double bad = std::numeric_limits<double>::quiet_NaN();
    Controls nan{std::numeric_limits<double>::quiet_NaN(), 0, 1};
    EXPECT_DOUBLE_EQ(stepTrain(bad, spec, nan, 0, 1), 0);
    EXPECT_DOUBLE_EQ(bad, 0);
    double v2 = 5;
    EXPECT_DOUBLE_EQ(stepTrain(v2, spec, idle, 0, -1), 0);  // no time, no motion
    EXPECT_DOUBLE_EQ(v2, 5);
}

TEST(TrainPhysics, BrakesHoldOnlyUpToTheirForce) {
    const TrainSpec spec;  // 400 t, full brake 0.12 g
    double v = 0;
    Controls full{0, 1, 0};
    for (int i = 0; i < 20; ++i) {
        stepTrain(v, spec, full, 0.15, 1.0);  // 15%: gravity 0.15 g, more than the brakes
    }
    EXPECT_LT(v, 0);  // it slides back despite full brake
    double held = 0;
    stepTrain(held, spec, full, 0.10, 5.0);  // 10%: within the brakes
    EXPECT_DOUBLE_EQ(held, 0);
}

TEST(TrainPhysics, AdhesionLimitsTractionAndBraking) {
    TrainSpec dry;
    TrainSpec wet = dry;
    wet.adhesion = 0.02;  // severe contamination
    double vd = 0, vw = 0;
    Controls go{1, 0, 1};
    stepTrain(vd, dry, go, 0, 1.0);
    stepTrain(vw, wet, go, 0, 1.0);
    EXPECT_GT(vd, vw);
    // On the wet rail the pull is the grip of the driven axles, less the resistance.
    const double grip = 0.02 * 9.81 * wet.drivenMassKg;
    EXPECT_NEAR(vw, (grip - wet.davisA) / wet.massKg * 1.0, 0.01);
    // Braking from speed: the wet rail brakes far less.
    double a = 30, b = 30;
    Controls stop{0, 1, 1};
    stepTrain(a, dry, stop, 0, 1.0);
    stepTrain(b, wet, stop, 0, 1.0);
    EXPECT_LT(a, b);
}

TEST(TrainPhysics, TrainsAreBuiltFromSourcedVehicles) {
    const auto w = freightWagon(80, 4);
    EXPECT_NEAR(w.davisA, 840, 1e-9);  // 6.5 x 80 + 80 x 4
    EXPECT_NEAR(w.davisB, 13.248, 1e-9);
    EXPECT_NEAR(w.davisC, 1.24416, 1e-12);
    std::vector<VehicleSpec> consist = {electricLocomotive()};
    for (int i = 0; i < 10; ++i) {
        consist.push_back(freightWagon(80, 4));
    }
    const auto t = makeTrain(consist);
    EXPECT_NEAR(t.massKg, 890'000, 1e-6);
    EXPECT_NEAR(t.davisA, 8400 + electricLocomotive().davisA, 1e-6);  // total forces add up
    EXPECT_NEAR(t.davisC, 12.4416 + electricLocomotive().davisC, 1e-9);
    EXPECT_NEAR(t.maxPowerW, 6'400'000, 1e-6);
    EXPECT_NEAR(t.drivenMassKg, 90'000, 1e-6);  // only the locomotive pulls
    EXPECT_NE(t.source.find("RP-548"), std::string::npos);
    EXPECT_NE(t.source.find("Vectron"), std::string::npos);
    // The conversion agrees with the source formula in km/h at every speed:
    // R = 6.5 m + 80 n + 0.046 V m + 0.096 V^2 (V in km/h) = 2,168 N at 100 km/h.
    for (const double kmh : {0.0, 30.0, 100.0, 140.0}) {
        const double v = kmh / 3.6;
        EXPECT_NEAR(w.davisA + w.davisB * v + w.davisC * v * v, 6.5 * 80 + 80 * 4 + 0.046 * kmh * 80 + 0.096 * kmh * kmh, 1e-6);
    }

    const auto s = shinkansenSeries100();
    EXPECT_DOUBLE_EQ(s.davisA, 11'060);
    EXPECT_DOUBLE_EQ(s.davisB, 223.2);
    EXPECT_DOUBLE_EQ(s.davisC, 15.6168);
    EXPECT_NE(s.source.find("Scribd"), std::string::npos);  // says what it was checked against
    EXPECT_DOUBLE_EQ(kPassengerBrakeNotchG[3], 0.12);
}
