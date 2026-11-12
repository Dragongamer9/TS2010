// The own game's generated world and driving. No game files are read.
#include "game/Drive.h"
#include "game/GeneratedWorld.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

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
