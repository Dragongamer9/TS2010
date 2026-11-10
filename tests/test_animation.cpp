#include "JaTestBuilder.h"

#include "content/AnimationFile.h"
#include "content/ContentError.h"
#include "view/MeshPreview.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

using namespace trainz::content;
using namespace trainz::test;

namespace {

void putF(Bytes& b, float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    putU32(b, bits);
}
void putTag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }
Bytes chunk(const char* tag, const Bytes& data) {
    Bytes b;
    putTag(b, tag);
    putU32(b, static_cast<std::uint32_t>(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    return b;
}
void putText(Bytes& b, const std::string& s) {
    const std::size_t n = (s.size() + 4) / 4 * 4;
    putU32(b, static_cast<std::uint32_t>(n));
    Bytes t(s.begin(), s.end());
    t.resize(n, 0);
    b.insert(b.end(), t.begin(), t.end());
}
Bytes bone(const std::string& name, const std::vector<Bytes>& children, std::uint32_t claimedChildren = 99) {
    Bytes b;
    putU32(b, 100);
    putText(b, name);
    putU32(b, claimedChildren == 99 ? static_cast<std::uint32_t>(children.size()) : claimedChildren);
    for (const auto& c : children) {
        const Bytes bc = chunk("BONE", c);
        b.insert(b.end(), bc.begin(), bc.end());
    }
    return b;
}

// Like the bogie in Builtin\8.ja: b.r.ground with two wheels, 2 frames, 2 events.
Bytes anim(std::uint32_t frames = 2, std::uint32_t infoFrames = 2) {
    Bytes info;
    putU32(info, 100);
    putText(info, "AAR0bogy205.gmax");
    putU32(info, infoFrames);
    putU32(info, 30);
    putF(info, 1.0f);
    Bytes ev;
    putU32(ev, 100);
    putU32(ev, 2);
    putU32(ev, 1), putU32(ev, 0), putText(ev, "warble");
    putU32(ev, 1), putU32(ev, 4), putText(ev, "/warble");
    Bytes skel;
    putU32(skel, 100);
    const Bytes root = chunk("BONE", bone("b.r.ground", {bone("b.r.wheel01", {}), bone("b.r.wheel02", {})}));
    skel.insert(skel.end(), root.begin(), root.end());
    Bytes body;
    putTag(body, "ANIM");
    for (const Bytes& c : {chunk("INFO", info), chunk("EVNT", ev), chunk("SKEL", skel)}) {
        body.insert(body.end(), c.begin(), c.end());
    }
    for (std::uint32_t f = 0; f < frames; ++f) {
        Bytes fr;
        putU32(fr, 100);
        putU32(fr, f);
        for (int b = 0; b < 3; ++b) {
            putF(fr, 0), putF(fr, b == 1 ? -0.86f : (b == 2 ? 0.86f : 0)), putF(fr, b == 0 ? -0.1f : 0);
            if (f == 1 && b > 0) {  // 90 degrees about X for the wheels in frame 1
                putF(fr, 0.70710678f), putF(fr, 0), putF(fr, 0), putF(fr, 0.70710678f);
            } else {
                putF(fr, 0), putF(fr, 0), putF(fr, 0), putF(fr, 1);
            }
        }
        const Bytes fc = chunk("FRAM", fr);
        body.insert(body.end(), fc.begin(), fc.end());
    }
    Bytes b;
    putTag(b, "JIRF");
    putU32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

}  // namespace

TEST(Animation, ParsesLikeTheBogie) {
    const auto a = parseAnimation(anim());
    EXPECT_EQ(a.sourceFile, "AAR0bogy205.gmax");
    EXPECT_EQ(a.frameCount, 2u);
    EXPECT_EQ(a.framesPerSecond, 30u);
    ASSERT_EQ(a.bones.size(), 3u);
    EXPECT_EQ(a.bones[0].name, "b.r.ground");
    EXPECT_EQ(a.bones[0].parent, -1);
    EXPECT_EQ(a.bones[1].parent, 0);
    EXPECT_EQ(a.bones[2].name, "b.r.wheel02");
    EXPECT_EQ(a.boneIndex("b.r.wheel01"), 1);
    EXPECT_EQ(a.boneIndex("nope"), -1);
    ASSERT_EQ(a.events.size(), 2u);
    EXPECT_EQ(a.events[1].name, "/warble");
    EXPECT_EQ(a.events[1].flags, 4u);
    ASSERT_EQ(a.frames.size(), 2u);
    EXPECT_FLOAT_EQ(a.frames[0][1].position[1], -0.86f);
    EXPECT_FLOAT_EQ(a.frames[1][2].rotation[0], 0.70710678f);
}

TEST(Animation, Rotate) {
    const std::array<float, 4> q90z = {0, 0, 0.70710678f, 0.70710678f};
    const auto v = rotate(q90z, {1, 0, 0});
    EXPECT_NEAR(v[0], 0, 1e-6);
    EXPECT_NEAR(v[1], 1, 1e-6);
    EXPECT_NEAR(v[2], 0, 1e-6);
}

TEST(Animation, Errors) {
    EXPECT_THROW(parseAnimation(anim(2, 3)), ContentError);  // INFO frame count differs
    EXPECT_THROW(parseAnimation(anim(0, 1)), ContentError);
    Bytes bad = anim();
    bad[8] = 'X';  // form
    EXPECT_THROW(parseAnimation(bad), ContentError);
}

TEST(Animation, EveryTruncationAndByteChangeIsSafe) {
    const Bytes good = anim();
    auto tryParse = [](const Bytes& b) {
        try {
            (void)parseAnimation(b);
        } catch (const ContentError&) {
        }
    };
    for (std::size_t len = 0; len < good.size(); ++len) {
        tryParse(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len)));
    }
    for (std::size_t i = 0; i < good.size(); ++i) {
        for (std::uint8_t v : {std::uint8_t{0}, std::uint8_t{0xFF}, std::uint8_t{0x03}}) {
            Bytes b = good;
            b[i] = v;
            tryParse(b);
        }
    }
}

TEST(Animation, PoseMeshMovesBonedParts) {
    const auto a = parseAnimation(anim());
    MeshFile m;
    MeshPart wheel;
    wheel.geometry.boneName = "b.r.wheel01";
    wheel.geometry.vertices = {MeshVertex{0, 0, 0.43f, 0, 0}};
    wheel.geometry.vertexNormals = {{0, 0, 1}};
    MeshPart fixed;
    fixed.geometry.vertices = {MeshVertex{1, 2, 3, 0, 0}};
    m.parts = {wheel, fixed};
    EXPECT_EQ(trainz::view::poseMesh(m, a, 1), 1u);
    // 90 degrees about X turns (0, 0, 0.43) into (0, -0.43, 0), then the bone position is added
    EXPECT_NEAR(m.parts[0].geometry.vertices[0].y, -0.86f - 0.43f, 1e-5);
    EXPECT_NEAR(m.parts[0].geometry.vertices[0].z, 0.0f, 1e-5);
    EXPECT_NEAR(m.parts[0].geometry.vertexNormals[0][1], -1.0f, 1e-5);
    EXPECT_FLOAT_EQ(m.parts[1].geometry.vertices[0].x, 1.0f);  // no bone: unchanged
    EXPECT_THROW(trainz::view::poseMesh(m, a, 5), std::out_of_range);
}
