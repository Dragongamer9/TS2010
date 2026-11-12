// The game's own simulation core: driving along track and train physics. No game files.
#include "sim/TrackNetwork.h"
#include "sim/TrainPhysics.h"
#include "content/AssetConfig.h"
#include "content/EngineSpec.h"

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
)"));
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
