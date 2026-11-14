// The game's own simulation core: driving along track and train physics. No game files.
#include "sim/TrackNetwork.h"
#include "sim/TrainPhysics.h"
#include "sim/Signals.h"
#include "content/PlacedObject.h"
#include "content/AssetConfig.h"
#include "content/EngineSpec.h"
#include "content/SignalAsset.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

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

TEST(TrackLayout, JunctionSettingFromTheVertex) {
    TrackVertex v;
    v.links = {{10, 0}, {11, 1}, {12, 1}};  // trunk 10 (lone flag), legs 11 and 12
    auto j = junctionSetting(v);
    ASSERT_TRUE(j.has_value());
    EXPECT_EQ(j->trunk, 10);
    EXPECT_EQ(j->legs, (std::vector<std::int32_t>{11, 12}));
    EXPECT_EQ(j->selected, 0u);
    v.lastByte = 1;
    EXPECT_EQ(junctionSetting(v)->selected, 1u);
    v.links = {{11, 1}, {12, 1}, {10, 0}};  // trunk in the last slot: legs keep slot order
    EXPECT_EQ(junctionSetting(v)->legs, (std::vector<std::int32_t>{11, 12}));
    v.lastByte = 2;  // past the last leg
    EXPECT_FALSE(junctionSetting(v).has_value());
    v.links = {{1, 0}, {2, 1}, {3, 1}, {4, 1}};  // three-way: 0, 1, 2
    EXPECT_EQ(junctionSetting(v)->selected, 2u);
    v.links = {{1, 0}, {2, 0}, {3, 1}, {4, 1}};  // crossing: two pairs, no trunk
    v.lastByte = 0;
    EXPECT_FALSE(junctionSetting(v).has_value());
    v.links = {{1, 0}, {2, 1}};  // not a junction
    EXPECT_FALSE(junctionSetting(v).has_value());
}

TEST(TrackNetwork, JunctionsStartAsStoredAndToggleAhead) {
    // As JunctionsTakeTheStraightWayUnlessSwitched, with the vertex set to its second leg (C).
    Layout l;
    l.vertex(0, 0, 0, {{1, 0}});
    l.vertex(1, 100, 0, {{1, 0}, {2, 1}, {3, 1}});
    l.t.vertices[1].lastByte = 1;
    l.vertex(2, 200, 0, {{2, 0}});
    l.vertex(3, 190, 40, {{3, 0}});
    l.piece(1, 0, 1);
    l.piece(2, 1, 2);
    l.piece(3, 1, 3);
    TrackNetwork n(l.t);
    ASSERT_EQ(n.switches.count(1), 1u);
    EXPECT_EQ(n.switches.at(1), 3);
    TrackPosition p{1, 90, +1};
    n.advance(p, 20);
    EXPECT_EQ(p.piece, 3);  // the stored setting, not the straight way
    const auto ahead = n.junctionAhead(TrackPosition{1, 30, +1});
    ASSERT_TRUE(ahead.has_value());
    EXPECT_EQ(ahead->vertex, 1);
    EXPECT_NEAR(ahead->distance, 70, 1e-6);
    EXPECT_EQ(ahead->ways, (std::vector<std::int32_t>{2, 3}));
    EXPECT_EQ(n.toggle(*ahead), 2);  // wraps round to the first way
    EXPECT_EQ(n.toggle(*ahead), 3);
    EXPECT_FALSE(n.junctionAhead(TrackPosition{1, 30, +1}, 50).has_value());  // too far
    EXPECT_FALSE(n.junctionAhead(TrackPosition{1, 30, -1}).has_value());      // behind: a line end
    EXPECT_FALSE(n.junctionAhead(TrackPosition{2, 10, -1}).has_value());      // trailing: no choice
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

// --- enginespec ------------------------------------------------------------------------
namespace {
trainz::content::ConfigNode configOf(const std::string& s) {
    return trainz::content::parseConfigText(std::vector<std::uint8_t>(s.begin(), s.end())).root;
}
}  // namespace

TEST(EngineSpec, ReadsMotorAndNotchCurves) {
    // Shaped like the SD40-2's enginespec <kuid:-1:42004221> on the disc (trimmed).
    const auto e = parseEngineSpec(configOf(R"(kind "engine"
motor
{
  adhesion 2.3
  maxspeed 35
  max-accel 25000
  axle-count 6
}
throttle-power
{
  0
  {
    0 0
  }
  1
  {
    0 165
    2.2 83
    13.0 0
  }
  2
  {
    4.4 200
    0 330
    22.2 0
  }
  junk
  {
    0 1
  }
}
dynamic-brake
{
  1
  {
    0 0
    10 120
  }
}
pressure
{
  scale 1
  brakepipe 0.00736041
  brakeinitial 0.00693861
  brakefull 0.0044992
  mainreservoir 0.00946941
}
)"));
    ASSERT_TRUE(e.brakePipe && e.brakeInitial && e.brakeFull && e.mainReservoir);
    EXPECT_FLOAT_EQ(static_cast<float>(*e.brakePipe), 0.00736041f);
    EXPECT_NEAR(approximatePsi(*e.mainReservoir), 90, 1e-3);
    EXPECT_NEAR(approximatePsi(*e.brakePipe), 70, 0.1);
    ASSERT_TRUE(e.adhesion && e.maxSpeed && e.maxAccel && e.axleCount);
    EXPECT_FLOAT_EQ(static_cast<float>(*e.adhesion), 2.3f);  // config values are 32-bit floats
    EXPECT_EQ(*e.axleCount, 6);
    ASSERT_EQ(e.throttlePower.size(), 3u);
    ASSERT_EQ(e.throttlePower[2].size(), 3u);
    EXPECT_FLOAT_EQ(static_cast<float>(e.throttlePower[2][1].first), 4.4f);  // sorted by speed
    EXPECT_DOUBLE_EQ(e.tractiveForceN(1, 0), 165'000);
    EXPECT_NEAR(e.tractiveForceN(1, 1.1), 124'000, 1);  // halfway between 165 and 83 kN
    EXPECT_NEAR(e.tractiveForceN(1, -1.1), 124'000, 1); // either direction
    EXPECT_DOUBLE_EQ(e.tractiveForceN(1, 20), 0);      // past the curve's end
    EXPECT_DOUBLE_EQ(e.tractiveForceN(9, 1), 0);       // no such notch
    const auto full = e.fullThrottleCurveN();
    ASSERT_EQ(full.size(), 3u);
    EXPECT_DOUBLE_EQ(full.front().second, 330'000);
    ASSERT_EQ(e.dynamicBrake.size(), 2u);
    EXPECT_EQ(e.dynamicBrake[1].size(), 2u);
}

TEST(EngineSpec, WagonOnlyMotorAndOddInput) {
    const auto wagon = parseEngineSpec(configOf("motor\n{\n  max-accel 3500\n  axle-count 4\n}\n"));
    EXPECT_TRUE(wagon.throttlePower.empty());
    EXPECT_TRUE(wagon.fullThrottleCurveN().empty());
    EXPECT_FALSE(wagon.adhesion.has_value());
    const auto none = parseEngineSpec(configOf("motor 3\nthrottle-power 4\n"));  // not groups
    EXPECT_FALSE(none.maxAccel.has_value());
    EXPECT_TRUE(none.throttlePower.empty());
    EXPECT_DOUBLE_EQ(interpolateCurve({}, 1), 0);
    EXPECT_DOUBLE_EQ(interpolateCurve({{0, 5}}, 3), 5);
    EXPECT_DOUBLE_EQ(interpolateCurve({{0, 5}, {2, 1}}, std::numeric_limits<double>::quiet_NaN()), 0);
}

TEST(TrainPhysics, TractiveCurvesAddUpAndLimitSpeed) {
    VehicleSpec a = freightWagon(100), b = freightWagon(100);
    a.driven = b.driven = true;
    a.tractiveCurveN = {{0, 200'000}, {10, 0}};
    b.tractiveCurveN = {{0, 100'000}, {5, 100'000}, {20, 0}};
    const auto t = makeTrain({a, b, freightWagon(50)});
    // Summed at every point of either curve.
    EXPECT_DOUBLE_EQ(interpolateCurve(t.tractiveCurveN, 0), 300'000);
    EXPECT_DOUBLE_EQ(interpolateCurve(t.tractiveCurveN, 5), 200'000);
    EXPECT_DOUBLE_EQ(interpolateCurve(t.tractiveCurveN, 10), 100'000 * 10.0 / 15 + 0);
    // Full throttle on the level: it speeds up but never past the curves' end (20 m/s).
    double speed = 0;
    for (int i = 0; i < 3600; ++i) {
        stepTrain(speed, t, Controls{1.0, 0.0, 1}, 0, 1.0);
    }
    EXPECT_GT(speed, 5);
    EXPECT_LT(speed, 20);
}

TEST(TrainPhysics, NotchCurvesSumPerNotchAndDrive) {
    VehicleSpec a = freightWagon(80), b = freightWagon(80);
    a.driven = b.driven = true;
    // Notch 0 idle; notch 1 weak; notch 2 stronger. Speeds in m/s, forces in N.
    a.notchCurvesN = {
        {{0, 0}},
        {{0, 50'000}, {20, 0}},
        {{0, 100'000}, {20, 0}},
    };
    b.notchCurvesN = {
        {{0, 0}},
        {{0, 25'000}, {10, 0}},
        {{0, 50'000}, {10, 50'000}, {30, 0}},
    };
    const auto t = makeTrain({a, b, freightWagon(40)});
    ASSERT_EQ(t.notchCurvesN.size(), 3u);
    EXPECT_DOUBLE_EQ(interpolateCurve(t.notchCurvesN[0], 0), 0);
    EXPECT_DOUBLE_EQ(interpolateCurve(t.notchCurvesN[1], 0), 75'000);   // 50k + 25k
    EXPECT_DOUBLE_EQ(interpolateCurve(t.notchCurvesN[2], 0), 150'000);  // 100k + 50k
    // Notch 1 at standstill pulls; notch 0 does not (smooth throttle ignored when notches exist).
    double v0 = 0, v1 = 0;
    stepTrain(v0, t, Controls{1.0, 0.0, 1, 0}, 0, 1.0);
    stepTrain(v1, t, Controls{0.0, 0.0, 1, 1}, 0, 1.0);
    EXPECT_DOUBLE_EQ(v0, 0);
    EXPECT_GT(v1, 0);
    // Higher notch reaches a higher speed before the curve ends.
    double lo = 0, hi = 0;
    for (int i = 0; i < 600; ++i) {
        stepTrain(lo, t, Controls{0, 0, 1, 1}, 0, 1.0);
        stepTrain(hi, t, Controls{0, 0, 1, 2}, 0, 1.0);
    }
    EXPECT_GT(hi, lo);
    EXPECT_LT(hi, 30);
    // Out-of-range notch: no traction (same as idle).
    double bad = 0;
    stepTrain(bad, t, Controls{1.0, 0.0, 1, 99}, 0, 1.0);
    EXPECT_DOUBLE_EQ(bad, 0);
}

TEST(TrainPhysics, SmoothThrottleWhenNoNotchCurves) {
    // No notchCurvesN: continuous throttle still uses starting force / power.
    TrainSpec spec;
    spec.massKg = 100'000;
    spec.maxTractiveForceN = 50'000;
    spec.maxPowerW = 1'000'000;
    spec.drivenMassKg = 100'000;
    spec.brakedMassKg = 100'000;
    spec.davisA = 1'000;
    double speed = 0;
    stepTrain(speed, spec, Controls{1.0, 0.0, 1, 5}, 0, 1.0);  // notch ignored
    EXPECT_GT(speed, 0);
    double idle = 0;
    stepTrain(idle, spec, Controls{0.0, 0.0, 1, 5}, 0, 1.0);
    EXPECT_DOUBLE_EQ(idle, 0);
}

// ChatGPT review of 1d045e1, finding 3: a curve that ends with a force must stop at its end
// when summed with a longer one, not spread over the gap to the other curve's next point.
TEST(TrainPhysics, SummedCurveKeepsAForceThatEndsAbruptly) {
    VehicleSpec a = freightWagon(50), b = freightWagon(50);
    a.tractiveCurveN = {{0, 100}, {10, 100}};
    b.tractiveCurveN = {{0, 50}, {20, 0}};
    const auto t = makeTrain({a, b});
    EXPECT_DOUBLE_EQ(interpolateCurve(t.tractiveCurveN, 5), 100 + 37.5);
    EXPECT_DOUBLE_EQ(interpolateCurve(t.tractiveCurveN, 10), 100 + 25);
    EXPECT_NEAR(interpolateCurve(t.tractiveCurveN, 15), 12.5, 1e-9);  // was 62.5
}

// Finding 4: a locomotive with curves and one without (starting force/power) both pull.
TEST(TrainPhysics, MixedLocomotivesBothPull) {
    VehicleSpec curved = freightWagon(90);
    curved.driven = true;
    curved.tractiveCurveN = {{0, 100'000}, {30, 0}};
    curved.notchCurvesN = {{{0, 0}}, {{0, 100'000}, {30, 0}}};
    const VehicleSpec preset = electricLocomotive();
    auto pulled = [](const TrainSpec& t, const Controls& c) {
        double v = 0;
        stepTrain(v, t, c, 0, 0.02);  // one step from a standstill: (pull - resistance) / mass
        return v * t.massKg / 0.02 + t.davisA;
    };
    TrainSpec both = makeTrain({curved, preset}, 1.0);  // plenty of grip
    EXPECT_NEAR(pulled(both, Controls{0, 0, 1, 1}), 100'000 + 300'000, 1);  // top notch: both in full
    double idle = 0;
    stepTrain(idle, both, Controls{0, 0, 1, 0}, 0, 1.0);  // idle: neither pulls
    EXPECT_DOUBLE_EQ(idle, 0);
    both.notchCurvesN.clear();                                               // smooth throttle path
    EXPECT_NEAR(pulled(both, Controls{0.5, 0, 1, 0}), 0.5 * (100'000 + 300'000), 1);
}

// Finding 2: bad numbers in a spec give finite results and never push the wrong way.
TEST(TrainPhysics, BadSpecValuesStayFiniteAndNeverReverse) {
    TrainSpec t = makeTrain({electricLocomotive(), freightWagon(40)});
    TrainSpec inf = t;
    inf.massKg = std::numeric_limits<double>::infinity();
    double v = 0;
    EXPECT_DOUBLE_EQ(stepTrain(v, inf, Controls{1, 0, 1, 0}, 0, 1.0), 0);
    EXPECT_DOUBLE_EQ(v, 0);
    TrainSpec neg = t;
    neg.maxTractiveForceN = -1e6, neg.maxPowerW = -1e6, neg.maxBrakeForceN = -1e6;
    neg.davisA = -1e5, neg.davisB = std::numeric_limits<double>::quiet_NaN();
    v = 0;
    stepTrain(v, neg, Controls{1, 1, 1, 0}, 0, 1.0);
    EXPECT_TRUE(std::isfinite(v));
    EXPECT_GE(v, 0);  // no backwards pull with the reverser forward
    v = 10;
    stepTrain(v, neg, Controls{0, 1, 1, 0}, 0, 1.0);
    EXPECT_TRUE(std::isfinite(v));
    EXPECT_LE(v, 10);  // negative brakes/resistance don't speed it up
}

// Cursor's open question: a 2-notch locomotive with an 8-notch one. The short one follows at
// the same fraction of its range, so both reach their top notch together.
TEST(TrainPhysics, LocomotivesWithDifferentNotchCountsShareTheRange) {
    VehicleSpec two = freightWagon(80), eight = freightWagon(80);
    two.driven = eight.driven = true;
    two.notchCurvesN = {{{0, 0}}, {{0, 1000}, {50, 0}}};
    for (int n = 0; n < 8; ++n) {
        eight.notchCurvesN.push_back({{0, 100.0 * n}, {50, 0}});
    }
    const auto t = makeTrain({two, eight});
    ASSERT_EQ(t.notchCurvesN.size(), 8u);
    EXPECT_DOUBLE_EQ(interpolateCurve(t.notchCurvesN[0], 0), 0);
    EXPECT_DOUBLE_EQ(interpolateCurve(t.notchCurvesN[3], 0), 300);         // 3/7 rounds to the short one's notch 0
    EXPECT_DOUBLE_EQ(interpolateCurve(t.notchCurvesN[4], 0), 1000 + 400);  // 4/7 rounds to its notch 1
    EXPECT_DOUBLE_EQ(interpolateCurve(t.notchCurvesN[7], 0), 1000 + 700);  // both at the top
    double prev = -1;
    for (const auto& c : t.notchCurvesN) {  // never weaker one notch up
        EXPECT_GE(interpolateCurve(c, 0), prev);
        prev = interpolateCurve(c, 0);
    }
}

// --- signals -----------------------------------------------------------------------------
TEST(TrackAttachment, ReadsPieceDistanceAndDirection) {
    PlacedObject o;
    o.tag = "sgOb";
    o.rest = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};  // 8 zeros, name length 1, "\0"
    const std::uint8_t tail[23] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0xb2, 0x0b, 0, 0, 0, 0, 0x80, 0x40, 1, 0, 0, 0, 2, 8};
    o.rest.insert(o.rest.end(), tail, tail + 23);
    auto a = trackAttachment(o);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->piece, 0x0bb2);
    EXPECT_FLOAT_EQ(a->distance, 4.0f);
    EXPECT_TRUE(a->alongPiece);
    EXPECT_EQ(a->tail[3], 2);
    EXPECT_EQ(a->tail[4], 8);
    o.rest[o.rest.size() - 23 + 17] = 0;
    EXPECT_FALSE(trackAttachment(o)->alongPiece);
    o.rest[o.rest.size() - 23 + 17] = 2;  // not a direction
    EXPECT_FALSE(trackAttachment(o).has_value());
    o.rest[o.rest.size() - 23 + 17] = 1;
    o.rest[o.rest.size() - 23] = 3;  // the tail doesn't start 2
    EXPECT_FALSE(trackAttachment(o).has_value());
    o.rest.resize(10);
    EXPECT_FALSE(trackAttachment(o).has_value());
}

TEST(Signals, BlocksStopCautionAndClear) {
    // A 1 km line 0 -> 1 -> 2 -> 3 (pieces 1, 2, 3 of ~333 m) with signals at the start of each
    // piece for +x travel, and one for -x travel on piece 2.
    Layout l;
    l.vertex(0, 0, 0, {{1, 0}});
    l.vertex(1, 333, 0, {{1, 0}, {2, 1}});
    l.vertex(2, 666, 0, {{2, 0}, {3, 1}});
    l.vertex(3, 999, 0, {{3, 0}});
    l.piece(1, 0, 1);
    l.piece(2, 1, 2);
    l.piece(3, 2, 3);
    TrackNetwork n(l.t);
    SignalSystem s(n, {{{1, 10, +1}, "A"}, {{2, 10, +1}, "B"}, {{3, 10, +1}, "C"}, {{2, 300, -1}, "back"}});
    s.update({});
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(s.aspect(i), Aspect::Clear) << i;
    }
    s.update({{3, 100}});  // something in C's block
    EXPECT_EQ(s.aspect(2), Aspect::Stop);
    EXPECT_EQ(s.aspect(1), Aspect::Caution);  // B: clear block, but C at stop
    EXPECT_EQ(s.aspect(0), Aspect::Clear);
    EXPECT_EQ(s.aspect(3), Aspect::Clear);  // for the other direction: its block runs back toward 0
    s.update({{2, 100}});  // in B's block, and in "back"'s block (between it and the line end)
    EXPECT_EQ(s.aspect(1), Aspect::Stop);
    EXPECT_EQ(s.aspect(0), Aspect::Caution);
    EXPECT_EQ(s.aspect(3), Aspect::Stop);
    // The next signal ahead, only for the direction of travel.
    const auto ahead = s.nextSignal(TrackPosition{1, 100, +1});
    ASSERT_TRUE(ahead.has_value());
    EXPECT_EQ(ahead->index, 1u);
    EXPECT_NEAR(ahead->distance, n.length(1).value() - 100 + 10, 1e-6);
    const auto back = s.nextSignal(TrackPosition{3, 100, -1});
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->index, 3u);
    EXPECT_FALSE(s.nextSignal(TrackPosition{3, 100, +1}).has_value());  // nothing further on
    EXPECT_FALSE(s.nextSignal(TrackPosition{1, 100, +1}, 50).has_value());  // too far
}

TEST(Signals, NextSignalOwnsOccupiedPointOnSamePiece) {
    Layout l;
    l.vertex(0, 0, 0, {{1, 0}});
    l.vertex(1, 200, 0, {{1, 0}});
    l.piece(1, 0, 1);
    TrackNetwork n(l.t);
    SignalSystem s(n, {{{1, 10, +1}, "A"}, {{1, 100, +1}, "B"}});
    s.update({{1, 100}});
    EXPECT_EQ(s.aspect(0), Aspect::Caution);
    EXPECT_EQ(s.aspect(1), Aspect::Stop);
}

TEST(Signals, NextSignalOwnsOccupiedPointAtPieceStart) {
    Layout l;
    l.vertex(0, 0, 0, {{1, 0}});
    l.vertex(1, 100, 0, {{1, 0}, {2, 1}});
    l.vertex(2, 200, 0, {{2, 0}});
    l.piece(1, 0, 1);
    l.piece(2, 1, 2);
    TrackNetwork n(l.t);
    SignalSystem s(n, {{{1, 10, +1}, "A"}, {{2, 0, +1}, "B"}});
    s.update({{2, 0}});
    EXPECT_EQ(s.aspect(0), Aspect::Caution);
    EXPECT_EQ(s.aspect(1), Aspect::Stop);
}

TEST(Signals, NextSignalOwnsOccupiedPointStoredAtPreviousPieceEnd) {
    Layout l;
    l.vertex(0, 0, 0, {{1, 0}});
    l.vertex(1, 100, 0, {{1, 0}, {2, 1}});
    l.vertex(2, 200, 0, {{2, 0}});
    l.piece(1, 0, 1);
    l.piece(2, 1, 2);
    TrackNetwork n(l.t);
    ASSERT_TRUE(n.length(1).has_value());
    SignalSystem s(n, {{{1, 10, +1}, "A"}, {{2, 0, +1}, "B"}});
    s.update({{1, *n.length(1)}});
    EXPECT_EQ(s.aspect(0), Aspect::Caution);
    EXPECT_EQ(s.aspect(1), Aspect::Stop);
}

TEST(Signals, BlockFollowsTheJunction) {
    // Signal on the trunk; the occupied leg only matters when the junction is set to it.
    Layout l;
    l.vertex(0, 0, 0, {{1, 0}});
    l.vertex(1, 100, 0, {{1, 0}, {2, 1}, {3, 1}});
    l.vertex(2, 200, 0, {{2, 0}});
    l.vertex(3, 190, 40, {{3, 0}});
    l.piece(1, 0, 1);
    l.piece(2, 1, 2);
    l.piece(3, 1, 3);
    TrackNetwork n(l.t);
    SignalSystem s(n, {{{1, 10, +1}, ""}});
    n.switches[1] = 2;
    s.update({{3, 50}});
    EXPECT_EQ(s.aspect(0), Aspect::Clear);
    n.switches[1] = 3;
    s.update({{3, 50}});
    EXPECT_EQ(s.aspect(0), Aspect::Stop);
}

TEST(SignalAsset, StatesLightsAndColours) {
    const auto look = parseSignalLook(configOf(R"(kind "mosignal"
signals
{
  1
  {
    light 2
  }
  4
  {
    light 1
  }
  7
  {
    light 1 3
  }
  8
  {
    light 0
  }
}
lights
{
  0
  {
    corona "corona_green.tga"
  }
  1
  {
    corona "corona_yellow.tga"
  }
  2
  {
    corona "corona_red.tga"
  }
}
)"));
    ASSERT_EQ(look.states.size(), 4u);
    EXPECT_EQ(look.states.at(7), (std::vector<int>{1, 3}));
    EXPECT_EQ(look.coronas.at(2), "corona_red.tga");
    EXPECT_EQ(stateForAspect(look, 0), 1);  // no state 0: the lowest of 0-3
    EXPECT_EQ(stateForAspect(look, 1), 4);
    EXPECT_EQ(stateForAspect(look, 2), 8);
    SignalLook twoAspect;
    twoAspect.states = {{0, {0}}, {8, {1}}};
    EXPECT_EQ(stateForAspect(twoAspect, 1), 0);  // no caution state: shows stop
    SignalLook onlyStop;
    onlyStop.states = {{1, {0}}};
    EXPECT_EQ(stateForAspect(onlyStop, 2), 1);
    EXPECT_FALSE(stateForAspect(SignalLook{}, 0).has_value());
    EXPECT_EQ(coronaColour("Corona_RED.tga")[0], 255);
    EXPECT_GT(coronaColour("corona_green.tga")[1], coronaColour("corona_green.tga")[0]);
    EXPECT_EQ(coronaColour("lunar.tga"), coronaColour("something.tga"));
}

TEST(TrainPhysics, DynamicBrakeSlowsWithItsCurveAndCutsTraction) {
    VehicleSpec loco = freightWagon(180, 6), two = freightWagon(180, 6);
    loco.driven = two.driven = true;
    loco.notchCurvesN = {{{0, 0}}, {{0, 300'000}, {30, 0}}};
    loco.dynamicBrakeCurvesN = {{{0, 0}}, {{1.333, 0}, {5, 100'000}, {25, 50'000}, {30, 0}}};
    two.dynamicBrakeCurvesN = {{{0, 0}}, {{1.333, 0}, {5, 100'000}, {25, 50'000}, {30, 0}}};
    const auto t = makeTrain({loco, two, freightWagon(60)});
    ASSERT_EQ(t.dynamicBrakeCurvesN.size(), 2u);
    EXPECT_DOUBLE_EQ(interpolateCurve(t.dynamicBrakeCurvesN[1], 5), 200'000);  // both locomotives
    auto coast = [&](Controls c) {  // 10 s (stepTrain takes at most 1 s a call)
        double v = 15;
        for (int k = 0; k < 10; ++k) {
            stepTrain(v, t, c, 0, 1.0);
        }
        return v;
    };
    const double free = coast(Controls{0, 0, 1, 0, 0});
    const double dyn = coast(Controls{0, 0, 1, 0, 1});
    EXPECT_LT(dyn, free - 0.5);                         // it brakes
    EXPECT_NEAR(coast(Controls{0, 0, 1, 1, 1}), dyn, 1e-9);  // a notch up changes nothing while the dynamic brake is on
    // Below its curve's start (1.333 m/s) it does nothing: it never holds a train at a stop.
    double slow = 1.0;
    stepTrain(slow, t, Controls{0, 0, 1, 0, 1}, 0, 0.02);
    double slowFree = 1.0;
    stepTrain(slowFree, t, Controls{0, 0, 1, 0, 0}, 0, 0.02);
    EXPECT_NEAR(slow, slowFree, 1e-12);
    EXPECT_NEAR(coast(Controls{0, 0, 1, 0, 9}), free, 1e-9);  // an unknown dynamic notch is off
}

TEST(TrackNetwork, RouteFindsTheWayAndTheJunctionSettings) {
    // 0 --1-- 1, then 2 straight on to (200, 0) and 3 off to (190, 40); 4 beyond 3 to (280, 80).
    Layout l;
    l.vertex(0, 0, 0, {{1, 0}});
    l.vertex(1, 100, 0, {{1, 0}, {2, 1}, {3, 1}});
    l.vertex(2, 200, 0, {{2, 0}});
    l.vertex(3, 190, 40, {{3, 0}, {4, 1}});
    l.vertex(4, 280, 80, {{4, 0}});
    l.piece(1, 0, 1);
    l.piece(2, 1, 2);
    l.piece(3, 1, 3);
    l.piece(4, 3, 4);
    TrackNetwork n(l.t);
    const auto r = n.route({1, 10, +1}, {4, 50, +1});
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->switches.size(), 1u);
    EXPECT_EQ(r->switches[0], (std::pair<std::int32_t, std::int32_t>{1, 3}));  // set the junction to the branch
    EXPECT_NEAR(r->distance, 90 + n.length(3).value() + 50, 1e-6);
    // Straight on: the junction set to piece 2.
    const auto s = n.route({1, 10, +1}, {2, 30, +1});
    ASSERT_TRUE(s.has_value());
    ASSERT_EQ(s->switches.size(), 1u);
    EXPECT_EQ(s->switches[0].second, 2);
    // Same piece, ahead: no junctions. Behind (no reversing) or too far: none.
    EXPECT_NEAR(n.route({1, 10, +1}, {1, 60, +1})->distance, 50, 1e-9);
    EXPECT_FALSE(n.route({1, 60, +1}, {1, 10, +1}).has_value());
    EXPECT_FALSE(n.route({1, 10, +1}, {4, 50, +1}, 100).has_value());
    EXPECT_FALSE(n.route({2, 10, +1}, {4, 50, +1}).has_value());  // from the straight leg it cannot get there
}

// The 31-byte trackside form (trackmarks, triggers, junction levers), as in the trackmark "B Stop".
TEST(TrackAttachment, ReadsTheTrackmarkForm) {
    PlacedObject o;
    o.tag = "tsO2";
    o.rest = {0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 'B', ' ', 'S', 't', 'o', 'p', 0};
    const std::uint8_t tail[31] = {2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xa0, 0x41, 0, 0, 0, 0, 0xd7, 0x08, 0, 0,
                                   0, 0, 0x3e, 0x42, 1, 0, 0, 0, 0, 0};
    o.rest.insert(o.rest.end(), tail, tail + 31);
    const auto a = trackAttachment(o);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->piece, 0x08d7);
    EXPECT_FLOAT_EQ(a->distance, 47.5f);
    EXPECT_TRUE(a->alongPiece);
    o.rest[o.rest.size() - 31 + 25] = 0;
    EXPECT_FALSE(trackAttachment(o)->alongPiece);
    o.rest[o.rest.size() - 31 + 4] = 7;  // not the form's head: and not the 23-byte form either
    EXPECT_FALSE(trackAttachment(o).has_value());
}

TEST(TrackNetwork, ShuntingRouteReversesThroughAHeadshunt) {
    // Junction at vertex 1 (100, 0): the trunk (piece 1) runs east to vertex 2; legs A (piece 2,
    // from vertex 3) and B (piece 3, to vertex 4) both leave it westward.
    Layout l;
    l.vertex(1, 100, 0, {{1, 0}, {2, 1}, {3, 1}});
    l.vertex(2, 300, 0, {{1, 0}});
    l.vertex(3, 0, 10, {{2, 0}});
    l.vertex(4, 0, -10, {{3, 0}});
    l.piece(1, 1, 2);
    l.piece(2, 3, 1);
    l.piece(3, 1, 4);
    TrackNetwork n(l.t);
    const TrackPosition onA{2, 20, +1};   // on leg A, heading for the junction
    const TrackPosition onB{3, 50, +1};   // a place on leg B
    EXPECT_FALSE(n.route(onA, onB).has_value());  // no way without reversing
    const auto r = n.routeWithReversals(onA, onB, 60);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->legs.size(), 2u);
    EXPECT_TRUE(r->legs[0].reverseAfter);
    EXPECT_EQ(r->legs[0].end.piece, 1);           // onto the trunk...
    EXPECT_NEAR(r->legs[0].end.offset, 60, 1e-9);  // ...60 m in (clear of the junction)
    ASSERT_EQ(r->legs[1].switches.size(), 1u);
    EXPECT_EQ(r->legs[1].switches[0], (std::pair<std::int32_t, std::int32_t>{1, 3}));  // back onto leg B
    EXPECT_EQ(r->legs[1].end.piece, 3);
    const double legA = n.length(2).value() - 20;
    EXPECT_NEAR(r->distance, legA + 2 * 60 + 50, 1e-6);
    // Too short a trunk to stand clear on: no way.
    EXPECT_FALSE(n.routeWithReversals(onA, onB, 250).has_value());
}
