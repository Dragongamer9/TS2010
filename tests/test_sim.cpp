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
