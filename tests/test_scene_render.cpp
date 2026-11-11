#include "content/AssetConfig.h"
#include "view/AssetMeshes.h"
#include "view/RouteScene.h"
#include "view/GroundTextures.h"
#include "view/FlyCamera.h"
#include "view/RouteCatalog.h"
#include "view/SceneRender.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace trainz::view;

namespace {

const std::uint8_t* px(const std::vector<std::uint8_t>& img, std::uint32_t w, std::uint32_t x, std::uint32_t y) {
    return img.data() + (static_cast<std::size_t>(y) * w + x) * 4;
}

Camera lookingDown() {
    Camera c;
    c.eye = {0, -0.01, 100};  // almost straight down, so +y is up on screen
    c.target = {0, 0, 0};
    c.fovYDegrees = 60;
    return c;
}

SceneTriangle flat(double z, Rgb colour, double size) {
    SceneTriangle t;
    t.p[0] = {-size, -size, z};
    t.p[1] = {size, -size, z};
    t.p[2] = {0, size, z};
    t.c[0] = t.c[1] = t.c[2] = colour;
    return t;
}

}  // namespace

TEST(SceneRender, TriangleCoversTheCentreSkyElsewhere) {
    Scene s;
    s.triangles.push_back(flat(0, {200, 0, 0}, 20));
    const auto img = renderScene(s, lookingDown(), 64, 64);
    const auto* c = px(img, 64, 32, 32);
    EXPECT_GT(c[0], 80);  // red, shaded
    EXPECT_EQ(c[1], 0);
    const auto* corner = px(img, 64, 0, 0);
    EXPECT_GT(corner[2], 200);  // sky blue
}

TEST(SceneRender, NearerSurfaceWins) {
    Scene s;
    s.triangles.push_back(flat(0, {200, 0, 0}, 20));
    s.triangles.push_back(flat(10, {0, 200, 0}, 20));  // closer to the camera
    const auto img = renderScene(s, lookingDown(), 64, 64);
    EXPECT_GT(px(img, 64, 32, 32)[1], 80);
    EXPECT_EQ(px(img, 64, 32, 32)[0], 0);

    Scene t;  // same, drawn in the other order
    t.triangles.push_back(flat(10, {0, 200, 0}, 20));
    t.triangles.push_back(flat(0, {200, 0, 0}, 20));
    EXPECT_EQ(renderScene(t, lookingDown(), 64, 64), img);
}

TEST(SceneRender, LinesAndDotsShowOnTheirSurface) {
    Scene s;
    s.triangles.push_back(flat(0, {100, 100, 100}, 40));
    s.lines.push_back({{-10, 0, 0}, {10, 0, 0}, {255, 0, 0}});
    s.dots.push_back({{0, 10, 0}, {0, 0, 255}, 1});
    const auto img = renderScene(s, lookingDown(), 128, 128);
    bool red = false, blue = false;
    for (std::uint32_t y = 0; y < 128; ++y) {
        for (std::uint32_t x = 0; x < 128; ++x) {
            const auto* p = px(img, 128, x, y);
            red = red || (p[0] == 255 && p[1] == 0 && p[2] == 0);
            blue = blue || (p[0] == 0 && p[1] == 0 && p[2] == 255);
        }
    }
    EXPECT_TRUE(red);
    EXPECT_TRUE(blue);
}

TEST(SceneRender, BehindTheCameraAndBadInputAreSafe) {
    Scene s;
    s.triangles.push_back(flat(500, {200, 0, 0}, 20));  // above the camera, looking down: behind it
    SceneTriangle nanTri = flat(0, {0, 200, 0}, 20);
    nanTri.p[1].z = std::numeric_limits<double>::quiet_NaN();
    s.triangles.push_back(nanTri);
    s.lines.push_back({{0, 0, 500}, {0, 0, -500}, {255, 0, 0}});  // crosses the camera plane
    const auto img = renderScene(s, lookingDown(), 32, 32);
    EXPECT_GT(px(img, 32, 16, 16)[2], 200);  // nothing drawn: sky
    EXPECT_THROW(renderScene(s, lookingDown(), 0, 10), std::invalid_argument);
    EXPECT_THROW(renderScene(s, lookingDown(), 9000, 10), std::invalid_argument);
}

TEST(SceneRender, PlacedMeshIsRotatedAnticlockwiseThenMoved) {
    PreparedMesh m;
    LocalTriangle t{};
    t.p[0][0] = 1;  // (1, 0, 0)
    t.p[1][1] = 2;  // (0, 2, 0)
    t.p[2][2] = 3;  // (0, 0, 3)
    t.colour = {10, 20, 30};
    m.triangles.push_back(t);
    Scene s;
    addPlacedMesh(s, m, 100, 200, 5, std::numbers::pi / 2);  // a quarter turn
    ASSERT_EQ(s.triangles.size(), 1U);
    const auto& p = s.triangles[0].p;
    EXPECT_NEAR(p[0].x, 100, 1e-9);  // +x turned to +y
    EXPECT_NEAR(p[0].y, 201, 1e-9);
    EXPECT_NEAR(p[1].x, 98, 1e-9);  // +y turned to -x
    EXPECT_NEAR(p[1].y, 200, 1e-9);
    EXPECT_NEAR(p[2].z, 8, 1e-9);  // height kept, moved up by 5
    EXPECT_EQ(s.triangles[0].c[0].g, 20);
}

namespace {

trainz::content::ConfigNode config(const std::string& text) {
    return trainz::content::parseConfigText(std::vector<std::uint8_t>(text.begin(), text.end())).root;
}

}  // namespace

TEST(SceneRender, MeshSelectionSkipsLodsNightAndShadowsKeepsAttachedParts) {
    std::map<std::string, std::size_t> parts;
    const auto picked = selectAssetMeshes(config(
        "mesh-table\n{\n"
        "  lod-0\n  {\n    mesh \"lod0.im\"\n    lod-level 0\n    auto-create 1\n  }\n"
        "  lod-1\n  {\n    mesh \"lod1.im\"\n    lod-level 1\n    auto-create 1\n  }\n"
        "  default-night\n  {\n    mesh \"night/night.im\"\n    night-mesh-base \"lod-0\"\n  }\n"
        "  door\n  {\n    mesh \"door.im\"\n    att \"a.door\"\n    att-parent \"lod-0\"\n  }\n"
        "  shadow\n  {\n    mesh \"car_shadow.im\"\n  }\n"
        "  load\n  {\n    mesh \"load/load.im\"\n    auto-create 1\n  }\n"
        "  horn\n  {\n    mesh \"horn.im\"\n    att \"a.horn\"\n  }\n"
        "  odd\n  {\n    mesh \"odd.im\"\n    att 3\n  }\n"
        "}\n"),
        parts);
    EXPECT_EQ(picked, (std::vector<MeshChoice>{{"lod-0", "lod0.im", "", ""},
                                               {"door", "door.im", "a.door", "lod-0"},
                                               {"load", "load/load.im", "", ""},
                                               {"horn", "horn.im", "a.horn", "default"}}));  // no att-parent: "default"
    EXPECT_EQ(parts["mesh-table entries skipped: lower level of detail"], 1U);
    EXPECT_EQ(parts["mesh-table entries skipped: night variant"], 1U);
    EXPECT_EQ(parts["mesh-table entries skipped: shadow mesh"], 1U);
    EXPECT_EQ(parts["mesh-table entries skipped: unreadable att"], 1U);
}

TEST(SceneRender, MeshSelectionTopLevelAndAssetFilename) {
    std::map<std::string, std::size_t> parts;
    EXPECT_EQ(selectAssetMeshes(config("mesh \"tree.im\"\n"), parts), (std::vector<MeshChoice>{{"", "tree.im", "", ""}}));
    EXPECT_EQ(selectAssetMeshes(config("kind \"scenery\"\nasset-filename \"usa_building_brick_5\"\n"), parts),
              (std::vector<MeshChoice>{{"", "usa_building_brick_5.im", "", ""}}));
    EXPECT_TRUE(selectAssetMeshes(config("kind \"scenery\"\n"), parts).empty());
}

TEST(SceneRender, LodTextPicksTheHighestDetailMesh) {
    // The 70 t ore car's real .lm.txt (CR LF).
    const std::string ore =
        "version 1.0\r\noffset = 0.01;\r\ncalcPoint = center;\r\nmultiplier = 1.0;\r\n"
        "mesh(\"0.2\")\r\n{\r\nname=\"70ton_orecar_body_med.im\";\r\n}\r\n"
        "mesh(\"1.0\")\r\n{\r\nname=\"70ton_orecar_body.im\";\r\n}";
    EXPECT_EQ(highestDetailLodMesh(std::vector<std::uint8_t>(ore.begin(), ore.end())), "70ton_orecar_body.im");
    const std::string reversed = "mesh(\"1.0\") { name=\"hi\\a.im\"; } mesh(\"0.5\") { name=\"lo.im\"; }";
    EXPECT_EQ(highestDetailLodMesh(std::vector<std::uint8_t>(reversed.begin(), reversed.end())), "hi/a.im");
    for (const std::string& bad : {std::string(""), std::string("mesh(\"1.0\""), std::string("mesh(\"x\") { name=\"a.im\"; }"),
                                  std::string("mesh(") }) {
        (void)highestDetailLodMesh(std::vector<std::uint8_t>(bad.begin(), bad.end()));  // never crashes
    }
}

TEST(SceneRender, PlaceholderTreeIsMarkedAndShaped) {
    const PreparedMesh tree = placeholderTree();
    EXPECT_TRUE(tree.placeholder);
    ASSERT_EQ(tree.triangles.size(), 8U);
    for (const auto& t : tree.triangles) {
        EXPECT_FLOAT_EQ(t.p[2][2], 12.0F);  // apex
        EXPECT_FLOAT_EQ(t.p[0][2], 1.5F);   // base ring
    }
}

namespace {

// A 2x2 texture: top row red, green; bottom row blue, white. Alpha of the green texel is
// given, the rest solid.
SceneTexture checker(std::uint8_t greenAlpha) {
    SceneTexture t;
    t.width = t.height = 2;
    t.rgba = {255, 0, 0, 255, 0, 255, 0, greenAlpha, 0, 0, 255, 255, 255, 255, 255, 255};
    return t;
}

// A white square at z = 0 from (-20,-20) to (20,20), u along +x and v down the screen
// (v = 0 at +y, the top of the screen when looking down).
void texturedSquare(Scene& s, int texture) {
    SceneTriangle a, b;
    const ScenePoint p00{-20, 20, 0}, p10{20, 20, 0}, p01{-20, -20, 0}, p11{20, -20, 0};
    a.p[0] = p00, a.p[1] = p10, a.p[2] = p11;
    a.u[0] = 0, a.v[0] = 0, a.u[1] = 1, a.v[1] = 0, a.u[2] = 1, a.v[2] = 1;
    b.p[0] = p00, b.p[1] = p11, b.p[2] = p01;
    b.u[0] = 0, b.v[0] = 0, b.u[1] = 1, b.v[1] = 1, b.u[2] = 0, b.v[2] = 1;
    for (auto* t : {&a, &b}) {
        t->c[0] = t->c[1] = t->c[2] = {255, 255, 255};
        t->texture = texture;
        s.triangles.push_back(*t);
    }
}

}  // namespace

TEST(SceneRender, TextureIsSampledByUv) {
    const SceneTexture tex = checker(255);
    Scene s;
    s.textures.push_back(&tex);
    texturedSquare(s, 0);
    const auto img = renderScene(s, lookingDown(), 64, 64);
    const auto* tl = px(img, 64, 26, 26);  // top left of the square: red
    EXPECT_GT(tl[0], 100);
    EXPECT_LT(tl[1], 20);
    const auto* tr = px(img, 64, 38, 26);  // top right: green
    EXPECT_GT(tr[1], 100);
    EXPECT_LT(tr[0], 20);
    const auto* bl = px(img, 64, 26, 38);  // bottom left: blue
    EXPECT_GT(bl[2], 100);
    EXPECT_LT(bl[0], 20);
}

TEST(SceneRender, AlphaTestDropsClearTexelsOnlyWhenAsked) {
    SceneTexture tex = checker(0);
    Scene s;
    s.triangles.push_back(flat(-10, {0, 0, 200}, 40));  // a blue floor below
    s.textures.push_back(&tex);
    texturedSquare(s, 0);
    auto img = renderScene(s, lookingDown(), 64, 64);
    EXPECT_GT(px(img, 64, 38, 26)[1], 100);  // drawn solid green: no alpha test
    tex.alphaTest = true;
    img = renderScene(s, lookingDown(), 64, 64);
    const auto* through = px(img, 64, 38, 26);
    EXPECT_LT(through[1], 20);  // the floor shows through
    EXPECT_GT(through[2], 80);
    EXPECT_GT(px(img, 64, 26, 26)[0], 100);  // red still solid
}

TEST(SceneRender, BadTextureIndexOrDataDrawsUntextured) {
    SceneTexture broken;
    broken.width = broken.height = 4;  // no pixel data
    Scene s;
    s.textures.push_back(&broken);
    s.textures.push_back(nullptr);
    texturedSquare(s, 0);
    texturedSquare(s, 1);
    texturedSquare(s, 7);
    const auto img = renderScene(s, lookingDown(), 64, 64);
    const auto* c = px(img, 64, 32, 32);
    EXPECT_GT(c[0], 100);  // the white base colour, shaded
    EXPECT_GT(c[1], 100);
    EXPECT_GT(c[2], 100);
}

TEST(SceneRender, TexturedMeshSharesItsTexturesAcrossPlacements) {
    PreparedMesh m;
    m.textures.push_back(checker(255));
    LocalTriangle t{};
    t.p[1][0] = 1, t.p[2][1] = 1;
    t.texture = 0;
    t.uv[1][0] = 0.75F, t.uv[2][1] = 0.25F;
    t.colour = {1, 2, 3};
    m.triangles.push_back(t);
    LocalTriangle plain = t;
    plain.texture = -1;
    m.triangles.push_back(plain);
    Scene s;
    const SceneTexture other = checker(255);
    s.textures.push_back(&other);  // something already in the scene
    addPlacedMesh(s, m, 0, 0, 0, 0);
    addPlacedMesh(s, m, 10, 0, 0, 0);
    ASSERT_EQ(s.textures.size(), 2U);  // added once, after the existing one
    EXPECT_EQ(s.textures[1], &m.textures[0]);
    ASSERT_EQ(s.triangles.size(), 4U);
    EXPECT_EQ(s.triangles[0].texture, 1);
    EXPECT_EQ(s.triangles[2].texture, 1);
    EXPECT_FLOAT_EQ(s.triangles[0].u[1], 0.75F);
    EXPECT_FLOAT_EQ(s.triangles[0].v[2], 0.25F);
    EXPECT_EQ(s.triangles[0].c[0].r, 255);  // white: the texture gives the colour
    EXPECT_EQ(s.triangles[1].texture, -1);
    EXPECT_EQ(s.triangles[1].c[0].r, 1);  // untextured part keeps its flat colour
}

TEST(SceneRender, AlphaTestHeuristic) {
    auto image = [](std::size_t clear, std::size_t solid, std::size_t middle) {
        std::vector<std::uint8_t> rgba;
        for (std::size_t i = 0; i < clear + solid + middle; ++i) {
            rgba.insert(rgba.end(), {9, 9, 9, static_cast<std::uint8_t>(i < clear ? 0 : i < clear + solid ? 255 : 120)});
        }
        return rgba;
    };
    EXPECT_TRUE(wantsAlphaTest(image(40, 60, 0)));     // leaves: a clear/solid mix
    EXPECT_TRUE(wantsAlphaTest(image(40, 20, 40)));    // grass with soft edges (e.g. "erba 014": 36% between)
    EXPECT_TRUE(wantsAlphaTest(image(98, 2, 0)));      // thin blades: 2% solid
    EXPECT_FALSE(wantsAlphaTest(image(0, 100, 0)));    // all solid
    EXPECT_FALSE(wantsAlphaTest(image(2, 98, 0)));     // a few clear texels only
    EXPECT_FALSE(wantsAlphaTest(image(995, 5, 0)));    // nearly all clear: under 1% solid
    EXPECT_FALSE(wantsAlphaTest(image(20, 20, 60)));   // mostly smooth alpha: not a cut-out
    EXPECT_FALSE(wantsAlphaTest({}));
}

TEST(SceneRender, DistantSurfacesUseASmallerMip) {
    auto solid = [](std::uint32_t side, Rgb c) {
        SceneTexture t;
        t.width = t.height = side;
        for (std::uint32_t i = 0; i < side * side; ++i) {
            t.rgba.insert(t.rgba.end(), {c.r, c.g, c.b, 255});
        }
        return t;
    };
    // The square is about 22 x 22 pixels on screen: 64 x 64 texels are ~8 per pixel,
    // 24 x 24 about 1.2 (kept), 8 x 8 too coarse.
    SceneTexture big = solid(64, {255, 0, 0});
    big.smaller = {solid(24, {0, 255, 0}), solid(8, {0, 0, 255})};
    Scene s;
    s.textures.push_back(&big);
    texturedSquare(s, 0);
    auto img = renderScene(s, lookingDown(), 64, 64);
    EXPECT_GT(px(img, 64, 32, 32)[1], 100);  // the 24 level: within 2x of the pixels
    EXPECT_LT(px(img, 64, 32, 32)[0], 20);

    SceneTexture fine = solid(16, {255, 0, 0});  // already coarser than the pixels: kept
    fine.smaller = {solid(8, {0, 255, 0})};
    s.textures[0] = &fine;
    img = renderScene(s, lookingDown(), 64, 64);
    EXPECT_GT(px(img, 64, 32, 32)[0], 100);
    EXPECT_LT(px(img, 64, 32, 32)[1], 20);
}

TEST(SceneRender, NonFiniteUvsDrawTheBaseColour) {
    const SceneTexture tex = checker(255);
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max()}) {
        Scene s;
        s.textures.push_back(&tex);
        texturedSquare(s, 0);
        for (auto& t : s.triangles) {
            t.u[1] = bad, t.v[2] = bad;
        }
        const auto img = renderScene(s, lookingDown(), 64, 64);  // UBSan: no bad casts
        const auto* c = px(img, 64, 32, 32);
        if (std::isfinite(bad)) {
            EXPECT_GT(int{c[0]} + c[1] + c[2], 100);  // huge but finite: sampled (wrapped), drawn
        } else {
            EXPECT_GT(c[0], 100);  // not sampled: the white base colour, shaded
            EXPECT_GT(c[1], 100);
            EXPECT_GT(c[2], 100);
        }
    }
}

TEST(SceneRender, HugeTextureSidesAreRejectedWithoutOverflow) {
    SceneTexture huge;
    huge.width = huge.height = 0x80000000U;  // width * height * 4 wraps to 0 in 64 bits
    Scene s;
    s.textures.push_back(&huge);
    texturedSquare(s, 0);
    const auto img = renderScene(s, lookingDown(), 64, 64);  // ASan: no read past the empty data
    const auto* c = px(img, 64, 32, 32);
    EXPECT_GT(c[0], 100);  // drawn untextured: the white base colour, shaded
    EXPECT_GT(c[1], 100);
    EXPECT_GT(c[2], 100);

    SceneTexture top = checker(255);  // a bad smaller level is not used either
    top.smaller.push_back(huge);
    s.textures[0] = &top;
    EXPECT_NO_THROW(renderScene(s, lookingDown(), 8, 8));
}

TEST(SceneRender, LocalFrameAppliesAndComposes) {
    LocalFrame quarter;  // a quarter turn about z (x axis -> +y), then moved to (10, 0, 1)
    quarter.r[0] = 0, quarter.r[1] = 1, quarter.r[3] = -1, quarter.r[4] = 0;
    quarter.t[0] = 10, quarter.t[2] = 1;
    const float p[3] = {2, 0, 0};
    float q[3];
    quarter.apply(p, q);
    EXPECT_FLOAT_EQ(q[0], 10);
    EXPECT_FLOAT_EQ(q[1], 2);
    EXPECT_FLOAT_EQ(q[2], 1);

    LocalFrame lift;  // a point 3 m up inside the turned frame
    lift.t[2] = 3;
    const LocalFrame both = lift.then(quarter);
    float r[3];
    both.apply(p, r);  // (2,0,0) in lift -> (2,0,3) -> turned and moved
    EXPECT_FLOAT_EQ(r[0], 10);
    EXPECT_FLOAT_EQ(r[1], 2);
    EXPECT_FLOAT_EQ(r[2], 4);

    trainz::content::MeshAttachment a;
    a.position[1] = -2.3F;
    const LocalFrame f = LocalFrame::of(a);
    EXPECT_FLOAT_EQ(f.t[1], -2.3F);
    EXPECT_FLOAT_EQ(f.r[8], 1);
}

TEST(SceneRender, PlacedMeshWithInnerFrame) {
    PreparedMesh m;
    LocalTriangle t{};
    t.p[0][0] = 1;
    m.triangles.push_back(t);
    LocalFrame inner;
    inner.t[1] = 5;  // e.g. a bogie point 5 m along the body's +y
    Scene s;
    addPlacedMesh(s, m, inner, 100, 0, 0, std::numbers::pi / 2);
    ASSERT_EQ(s.triangles.size(), 1U);
    EXPECT_NEAR(s.triangles[0].p[0].x, 95, 1e-5);  // (1,5,0) turned a quarter: (-5,1,0)
    EXPECT_NEAR(s.triangles[0].p[0].y, 1, 1e-5);
}

TEST(SceneRender, BogieChoiceByPoint) {
    using trainz::content::Kuid;
    PreparedMesh car;
    EXPECT_FALSE(car.bogeyFor("a.bog0"));  // no bogie named
    car.bogey = Kuid{58422, 50120, std::nullopt};
    car.bogeys.emplace(1, PreparedMesh::BogeyEntry{Kuid{523, 156565, std::nullopt}, true});
    const auto b0 = car.bogeyFor("a.bog0");
    ASSERT_TRUE(b0);
    EXPECT_EQ(b0->kuid.content, 50120);  // the plain "bogey" for points the group doesn't name
    EXPECT_FALSE(b0->reversed);
    const auto b1 = car.bogeyFor("A.BOG1");
    ASSERT_TRUE(b1);
    EXPECT_EQ(b1->kuid.content, 156565);
    EXPECT_TRUE(b1->reversed);
    for (const char* other : {"a.bog", "a.limfront", "a.bogx", "a.bog0001", "b.bog0"}) {
        EXPECT_FALSE(car.bogeyFor(other)) << other;
    }
}

TEST(SceneRender, ConfigKuidReadsBareNumbers) {
    const auto root = config("bogey 100008\nother <kuid:58422:50120>\nbad -5\nname \"x\"\n");
    const auto bare = configKuid(*root.child("bogey"));
    ASSERT_TRUE(bare);
    EXPECT_EQ(bare->user, -1);
    EXPECT_EQ(bare->content, 100008);
    EXPECT_EQ(configKuid(*root.child("other"))->user, 58422);
    EXPECT_FALSE(configKuid(*root.child("bad")));
    EXPECT_FALSE(configKuid(*root.child("name")));
}

TEST(SceneRender, SceneTriangleBudget) {
    Scene s;
    EXPECT_TRUE(sceneHasRoomFor(s, kMaxSceneTriangles));
    EXPECT_FALSE(sceneHasRoomFor(s, kMaxSceneTriangles + 1));
    EXPECT_FALSE(sceneHasRoomFor(s, std::numeric_limits<std::size_t>::max()));  // no wrap-around
    s.triangles.resize(1);
    EXPECT_TRUE(sceneHasRoomFor(s, kMaxSceneTriangles - 1));
    EXPECT_FALSE(sceneHasRoomFor(s, kMaxSceneTriangles));  // e.g. one more bogie than fits
    EXPECT_TRUE(sceneHasRoomFor(s, 0));
}

TEST(SceneRender, HeadingAlongTheTrain) {
    const double pi = std::numbers::pi;
    EXPECT_NEAR(headingAlong(0.1, 0), 0.1, 1e-12);
    EXPECT_NEAR(headingAlong(pi + 0.1, 0), 0.1, 1e-12);     // the track the other way round
    EXPECT_NEAR(headingAlong(-pi + 0.2, 0), 0.2, 1e-12);
    EXPECT_NEAR(headingAlong(0.1, 2 * pi), 2 * pi + 0.1, 1e-12);  // stays next to ref
    EXPECT_NEAR(headingAlong(pi / 2 + 0.3, 0), -pi / 2 + 0.3, 1e-12);
    for (double h = -7; h < 7; h += 0.37) {
        for (double ref = -7; ref < 7; ref += 0.53) {
            const double r = headingAlong(h, ref);
            EXPECT_LE(std::fabs(r - ref), pi / 2 + 1e-12);
            EXPECT_NEAR(std::remainder(2 * (r - h), 2 * pi), 0, 1e-9);  // r = h or h + pi
        }
    }
}

TEST(SceneRender, SwivelTurnsAboutTheModelsVertical) {
    // A point at (1, 4, 0.5), tilted 30 degrees about its x axis (e.g. a bogie on a ramp).
    LocalFrame point;
    const float c30 = std::cos(0.5236F), s30 = std::sin(0.5236F);
    point.r[4] = c30, point.r[5] = s30, point.r[7] = -s30, point.r[8] = c30;  // y and z axes tilted
    point.t[0] = 1, point.t[1] = 4, point.t[2] = 0.5F;
    const double turn = std::numbers::pi / 2;
    const LocalFrame sw = swivelAboutModelZ(point, turn);
    // The position stays.
    const float origin[3] = {0, 0, 0};
    float o[3];
    sw.apply(origin, o);
    EXPECT_NEAR(o[0], 1, 1e-5);
    EXPECT_NEAR(o[1], 4, 1e-5);
    EXPECT_NEAR(o[2], 0.5, 1e-5);
    // Every axis of the point is turned a quarter about the MODEL's z: (x, y, z) -> (-y, x, z).
    for (int axis = 0; axis < 3; ++axis) {
        const float* before = point.r + 3 * axis;
        const float* after = sw.r + 3 * axis;
        EXPECT_NEAR(after[0], -before[1], 1e-5) << axis;
        EXPECT_NEAR(after[1], before[0], 1e-5) << axis;
        EXPECT_NEAR(after[2], before[2], 1e-5) << axis;  // the tilt is kept
    }
    // The old composition (swivel in the point's own axes) gets the tilted case wrong.
    LocalFrame spinLocal;
    spinLocal.r[0] = spinLocal.r[4] = 0, spinLocal.r[1] = 1, spinLocal.r[3] = -1;
    const LocalFrame wrong = spinLocal.then(point);
    EXPECT_GT(std::fabs(wrong.r[2] - sw.r[2]) + std::fabs(wrong.r[5] - sw.r[5]), 0.1);
    // Upright points: both agree (why the disc check could not tell them apart).
    LocalFrame upright;
    upright.t[1] = 4;
    const LocalFrame a = swivelAboutModelZ(upright, turn), b = spinLocal.then(upright);
    for (int i = 0; i < 9; ++i) {
        EXPECT_NEAR(a.r[i], b.r[i], 1e-5);
    }
}

namespace {

SceneTexture solidTexture(std::uint32_t side, Rgb c) {
    SceneTexture t;
    t.width = t.height = side;
    for (std::uint32_t i = 0; i < side * side; ++i) {
        t.rgba.insert(t.rgba.end(), {c.r, c.g, c.b, 255});
    }
    return t;
}

// An 8 x 8 vertex grid (4 x 4 cells), every vertex one layer of slot `slot` at full weight.
trainz::content::GroundSectionData groundGrid(std::uint8_t slot) {
    trainz::content::GroundSectionData d;
    d.vertices.resize(64);
    for (auto& v : d.vertices) {
        v.texture[0] = slot;
        v.weight[0] = 255;
    }
    return d;
}

}  // namespace

TEST(GroundTextures, BilinearSampleAndHalving) {
    SceneTexture t;
    t.width = t.height = 2;
    t.rgba = {0, 0, 0, 255, 200, 0, 0, 255, 0, 100, 0, 255, 200, 100, 40, 255};
    double c[3];
    sampleTextureBilinear(t, 0.5, 0.5, c);  // the middle: the average of all four
    EXPECT_NEAR(c[0], 100, 1e-9);
    EXPECT_NEAR(c[1], 50, 1e-9);
    EXPECT_NEAR(c[2], 10, 1e-9);
    sampleTextureBilinear(t, 0.25, 0.25, c);  // a texel centre: that texel
    EXPECT_NEAR(c[0], 0, 1e-9);
    sampleTextureBilinear(t, 0.0, 0.25, c);  // the left edge wraps to the right column
    EXPECT_NEAR(c[0], 100, 1e-9);

    const SceneTexture h = halveTexture(t);
    ASSERT_EQ(h.width, 1U);
    ASSERT_EQ(h.rgba.size(), 4U);
    EXPECT_EQ(h.rgba[0], 100);
    EXPECT_EQ(h.rgba[1], 50);
    SceneTexture odd = solidTexture(3, {9, 9, 9});
    EXPECT_EQ(halveTexture(odd).width, 1U);
    SceneTexture broken;
    broken.width = broken.height = 4;  // no pixel data
    EXPECT_EQ(halveTexture(broken).width, 0U);
}

TEST(GroundTextures, BakeBlendsLayersAndFallsBack) {
    const SceneTexture red = solidTexture(16, {200, 0, 0}), blue = solidTexture(16, {0, 0, 200});
    const std::vector<const SceneTexture*> slots = {&red, &blue};
    auto fallback = [](double, double) { return Rgb{1, 2, 3}; };

    const SceneTexture all = bakeGroundSection(groundGrid(0), 8, slots, 32, fallback);
    ASSERT_EQ(all.width, 32U);
    ASSERT_EQ(all.rgba.size(), 32U * 32 * 4);
    EXPECT_EQ(all.rgba[0], 200);
    EXPECT_EQ(all.rgba[2], 0);
    EXPECT_FALSE(all.smaller.empty());  // mips for distant terrain

    // Columns 0-1 red, 2-7 blue: the left edge red, the right blue, a blend between.
    auto d = groundGrid(0);
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t col = 2; col < 8; ++col) {
            d.vertices[row * 8 + col].texture[0] = 1;
        }
    }
    const SceneTexture mix = bakeGroundSection(d, 8, slots, 32, fallback);
    auto px = [&](std::uint32_t x, std::uint32_t y) { return mix.rgba.data() + (std::size_t{y} * 32 + x) * 4; };
    EXPECT_GT(px(1, 16)[0], 190);   // inside cell 0: all red
    EXPECT_GT(px(30, 16)[2], 190);  // right side: all blue
    const auto* between = px(12, 16);  // cell 1, 3/8 of the way from col 1 to col 2
    EXPECT_GT(between[0], 60);
    EXPECT_GT(between[2], 30);

    // A second layer at a lower weight mixes in proportionally.
    auto two = groundGrid(0);
    for (auto& v : two.vertices) {
        v.texture[1] = 1;
        v.weight[1] = 85;  // one third of the first layer's 255
    }
    const SceneTexture blend = bakeGroundSection(two, 8, slots, 8, fallback);
    EXPECT_NEAR(blend.rgba[0], 150, 1);  // 200 * 255/340
    EXPECT_NEAR(blend.rgba[2], 50, 1);

    // No usable texture: the fallback colour.
    const std::vector<const SceneTexture*> none = {nullptr, nullptr};
    const SceneTexture fb = bakeGroundSection(groundGrid(0), 8, none, 8, fallback);
    EXPECT_EQ(fb.rgba[0], 1);
    EXPECT_EQ(fb.rgba[2], 3);
    auto bad = groundGrid(7);  // slot number past the table, and 0xFF layers: ignored
    EXPECT_EQ(bakeGroundSection(bad, 8, slots, 8, fallback).rgba[1], 2);

    EXPECT_THROW(bakeGroundSection(groundGrid(0), 9, slots, 8, fallback), std::invalid_argument);  // 64 != 81
    EXPECT_THROW(bakeGroundSection(groundGrid(0), 8, slots, 0, fallback), std::invalid_argument);
    EXPECT_THROW(bakeGroundSection(groundGrid(0), 8, slots, 8, fallback, 0.0), std::invalid_argument);
}

TEST(SceneRender, CutOutColourIsNotBlendedWithClearTexels) {
    // Left column solid green, right column clear black. A pixel nearest the green texel but
    // close to the boundary must stay fully green (bilinear would mix in the black).
    SceneTexture tex;
    tex.width = tex.height = 2;
    tex.rgba = {0, 250, 0, 255, 0, 0, 0, 0, 0, 250, 0, 255, 0, 0, 0, 0};
    tex.alphaTest = true;
    Scene s;
    s.textures.push_back(&tex);
    texturedSquare(s, 0);
    const auto img = renderScene(s, lookingDown(), 64, 64);
    const int inside = px(img, 64, 25, 32)[1];  // u ~ 0.25: the green texel's centre
    const int nearEdge = px(img, 64, 30, 32)[1];  // u ~ 0.43: still nearest the green texel
    EXPECT_GT(inside, 150);
    EXPECT_NEAR(nearEdge, inside, 2);
    // Without the cut-out flag the colour is filtered (the dark neighbour mixes in).
    tex.alphaTest = false;
    const auto soft = renderScene(s, lookingDown(), 64, 64);
    EXPECT_LT(px(soft, 64, 30, 32)[1], inside - 30);
}

TEST(GroundTextures, BakedSectionsDoNotWrapAtTheirEdges) {
    SceneTexture t;
    t.width = t.height = 2;
    t.rgba = {200, 0, 0, 255, 0, 0, 200, 255, 200, 0, 0, 255, 0, 0, 200, 255};  // red left, blue right
    double c[3];
    sampleTextureBilinear(t, 0.0, 0.5, c);  // repeating: the left edge mixes in the right column
    EXPECT_NEAR(c[0], 100, 1e-9);
    t.clampEdges = true;
    sampleTextureBilinear(t, 0.0, 0.5, c);  // clamped: the left column only
    EXPECT_NEAR(c[0], 200, 1e-9);
    EXPECT_NEAR(c[2], 0, 1e-9);
    sampleTextureBilinear(t, 1.0, 0.5, c);  // and the right edge stays right (u = 1 does not wrap to 0)
    EXPECT_NEAR(c[2], 200, 1e-9);
    sampleTextureBilinear(t, std::numeric_limits<double>::quiet_NaN(), 1e300, c);  // safe on bad input
    EXPECT_TRUE(std::isfinite(c[0]));

    // The bake marks its image and every mip as clamped.
    const SceneTexture red = solidTexture(16, {200, 0, 0});
    const std::vector<const SceneTexture*> slots = {&red};
    const SceneTexture b = bakeGroundSection(groundGrid(0), 8, slots, 16, [](double, double) { return Rgb{}; });
    EXPECT_TRUE(b.clampEdges);
    ASSERT_FALSE(b.smaller.empty());
    for (const auto& m : b.smaller) {
        EXPECT_TRUE(m.clampEdges);
    }
}

namespace {

std::vector<std::uint8_t> srtHeader(const float box[6], const char* magic = "SRT 05.0.1") {
    std::vector<std::uint8_t> d(120, 0);
    std::memcpy(d.data(), magic, std::strlen(magic));
    std::memcpy(d.data() + 36, box, 24);
    return d;
}

// A w x h atlas, clear, with solid blocks (x0, y0, x1, y1) in the given colour.
SceneTexture atlasWith(std::uint32_t w, std::uint32_t h, const std::vector<std::array<std::uint32_t, 4>>& blocks) {
    SceneTexture t;
    t.width = w, t.height = h;
    t.rgba.assign(std::size_t{w} * h * 4, 0);
    for (const auto& b : blocks) {
        for (std::uint32_t y = b[1]; y < b[3]; ++y) {
            for (std::uint32_t x = b[0]; x < b[2]; ++x) {
                std::uint8_t* p = t.rgba.data() + (std::size_t{y} * w + x) * 4;
                p[0] = 40, p[1] = 120, p[2] = 40, p[3] = 255;
            }
        }
    }
    return t;
}

}  // namespace

TEST(SpeedTree, HeaderBox) {
    const float box[6] = {-21.4F, -17.7F, -0.5F, 22.8F, 25.4F, 33.0F};  // the acacia's numbers
    const auto b = readSpeedTreeBox(srtHeader(box));
    ASSERT_TRUE(b);
    EXPECT_FLOAT_EQ(b->min[0], -21.4F);
    EXPECT_FLOAT_EQ(b->max[2], 33.0F);
    EXPECT_FALSE(readSpeedTreeBox(srtHeader(box, "SRT 04.2.0")));  // only 5.0.1 is known
    EXPECT_FALSE(readSpeedTreeBox(std::vector<std::uint8_t>(40, 0)));
    float flat[6] = {0, 0, 5, 1, 1, 5};  // no height
    EXPECT_FALSE(readSpeedTreeBox(srtHeader(flat)));
    float bad[6] = {0, 0, 0, 1, 1, std::numeric_limits<float>::quiet_NaN()};
    EXPECT_FALSE(readSpeedTreeBox(srtHeader(bad)));
}

TEST(SpeedTree, FirstAtlasCellSkipsSpecks) {
    // Two tree images side by side in the lower half, and stray specks above them.
    const SceneTexture a = atlasWith(64, 64, {{20, 3, 21, 4}, {40, 6, 42, 8}, {4, 32, 28, 60}, {36, 30, 60, 62}});
    const auto c = firstAtlasCell(a);
    ASSERT_TRUE(c);
    EXPECT_EQ(c->x0, 4U);
    EXPECT_EQ(c->x1, 28U);
    EXPECT_EQ(c->y0, 32U);  // only the rows of the first image, not its neighbour's
    EXPECT_EQ(c->y1, 60U);
    EXPECT_FALSE(firstAtlasCell(atlasWith(64, 64, {})));                // nothing solid
    EXPECT_FALSE(firstAtlasCell(atlasWith(64, 64, {{10, 10, 12, 12}})));  // specks only
    SceneTexture broken;
    broken.width = broken.height = 8;
    EXPECT_FALSE(firstAtlasCell(broken));
}

TEST(SpeedTree, BillboardShape) {
    const float boxv[6] = {-10, -6, 0, 10, 14, 40};
    const auto box = readSpeedTreeBox(srtHeader(boxv));
    ASSERT_TRUE(box);
    const SceneTexture a = atlasWith(64, 64, {{0, 0, 32, 32}});
    const AtlasCell cell{0, 0, 32, 16};  // twice as wide as tall
    const PreparedMesh m = speedTreeBillboard(*box, a, cell, 0.5);
    EXPECT_TRUE(m.billboard);
    EXPECT_FALSE(m.placeholder);
    ASSERT_EQ(m.triangles.size(), 4U);  // two crossed quads
    ASSERT_EQ(m.textures.size(), 1U);
    EXPECT_TRUE(m.textures[0].alphaTest);
    float zmin = 1e9F, zmax = -1e9F, xmin = 1e9F, xmax = -1e9F;
    for (const auto& t : m.triangles) {
        EXPECT_EQ(t.texture, 0);
        for (int k = 0; k < 3; ++k) {
            zmin = std::min(zmin, t.p[k][2]), zmax = std::max(zmax, t.p[k][2]);
            xmin = std::min(xmin, t.p[k][0]), xmax = std::max(xmax, t.p[k][0]);
            EXPECT_GE(t.uv[k][0], 0.0F);
            EXPECT_LE(t.uv[k][0], 0.5F);  // inside the cell
            EXPECT_LE(t.uv[k][1], 0.25F);
        }
    }
    EXPECT_FLOAT_EQ(zmin, 0);
    EXPECT_FLOAT_EQ(zmax, 20);              // 40 x 0.5
    EXPECT_FLOAT_EQ(xmax - xmin, 40);       // 2 x the height, from the image's proportions
    EXPECT_FLOAT_EQ(0.5F * (xmin + xmax), 0);  // centred on the box (x)
    EXPECT_TRUE(speedTreeBillboard(*box, a, cell, 0).triangles.empty());
    EXPECT_TRUE(speedTreeBillboard(*box, a, AtlasCell{5, 5, 5, 9}, 1).triangles.empty());
}

TEST(SpeedTree, MeshScaleIsReadAsTheMean) {
    std::map<std::string, std::size_t> parts;
    const auto picked = selectAssetMeshes(config("mesh-table\n{\n  default\n  {\n    mesh \"tree.srt\"\n    mesh-scale 0.4,0.5\n  }\n"
                                                 "  other\n  {\n    mesh \"x.im\"\n  }\n}\n"),
                                          parts);
    ASSERT_EQ(picked.size(), 2U);
    EXPECT_NEAR(picked[0].scale, 0.45, 1e-6);
    EXPECT_DOUBLE_EQ(picked[1].scale, 1.0);
}

TEST(FlyCamera, TakesOverAViewAndMovesLevel) {
    Camera c;
    c.eye = {0, 0, 10};
    c.target = {10, 0, 0};  // looking along +x, 45 degrees down
    auto f = FlyCamera::lookingAlong(c);
    EXPECT_NEAR(f.heading, 0, 1e-12);
    EXPECT_NEAR(f.pitch, -std::numbers::pi / 4, 1e-12);
    const Camera back = f.camera(55);
    EXPECT_NEAR(back.eye.z, 10, 1e-12);
    EXPECT_GT(back.target.x, back.eye.x);  // still looking along +x and down
    EXPECT_LT(back.target.z, back.eye.z);
    EXPECT_DOUBLE_EQ(back.fovYDegrees, 55);

    f.move(5, 0, 0);  // forward stays level even when looking down
    EXPECT_NEAR(f.position.x, 5, 1e-12);
    EXPECT_NEAR(f.position.z, 10, 1e-12);
    f.move(0, 2, 0);  // right of +x is -y
    EXPECT_NEAR(f.position.y, -2, 1e-12);
    f.move(0, 0, 3);
    EXPECT_NEAR(f.position.z, 13, 1e-12);

    f.turn(std::numbers::pi / 2, 0);  // a quarter turn left: now along +y
    f.move(1, 0, 0);
    EXPECT_NEAR(f.position.y, -1, 1e-12);
    f.turn(0, 10);  // pitch is held short of straight up
    EXPECT_NEAR(f.pitch, FlyCamera::kMaxPitch, 1e-12);
    f.turn(100 * std::numbers::pi + 0.25, 0);  // heading stays a small angle
    EXPECT_LE(std::fabs(f.heading), std::numbers::pi);

    const auto before = f.position;
    f.move(std::numeric_limits<double>::quiet_NaN(), 0, 0);  // ignored
    f.turn(std::numeric_limits<double>::infinity(), 0);
    EXPECT_DOUBLE_EQ(f.position.x, before.x);
    EXPECT_TRUE(std::isfinite(f.heading));

    Camera down;  // straight down: no heading to take, but a pitch
    down.eye = {0, 0, 10};
    down.target = {0, 0, 0};
    EXPECT_NEAR(FlyCamera::lookingAlong(down).pitch, -FlyCamera::kMaxPitch, 1e-12);
}

TEST(RouteCatalog, FindByNameOrKuid) {
    using trainz::content::Kuid;
    const CatalogEntry a{Kuid{107727, 1019, std::nullopt}, "CN Holly Subdivision", {}};
    const CatalogEntry b{Kuid{143919, 27565, 1}, "Winter in Germany", {}};
    const CatalogEntry c{Kuid{-1, 100858, std::nullopt}, "Winter in the Alps", {}};
    const CatalogEntry d{Kuid{5, 6, std::nullopt}, "Winter", {}};
    const std::vector<const CatalogEntry*> all = {&a, &b, &c};
    std::vector<std::string> cand;
    EXPECT_EQ(findEntry(all, "holly", cand), &a);  // part of one name
    EXPECT_TRUE(cand.empty());
    EXPECT_EQ(findEntry(all, "WINTER IN THE ALPS", cand), &c);  // exact, any case
    EXPECT_EQ(findEntry(all, "winter", cand), nullptr);  // two match
    EXPECT_EQ(cand, (std::vector<std::string>{"Winter in Germany  <kuid2:143919:27565:1>", "Winter in the Alps  <kuid:-1:100858>"}));
    const std::vector<const CatalogEntry*> withExact = {&b, &c, &d};
    EXPECT_EQ(findEntry(withExact, "winter", cand), &d);  // an exact name wins over parts
    EXPECT_EQ(findEntry(all, "<kuid:107727:1019>", cand), &a);
    EXPECT_EQ(findEntry(all, "<kuid2:143919:27565:9>", cand), &b);  // any version
    EXPECT_EQ(findEntry(all, "<kuid:1:2>", cand), nullptr);
    EXPECT_EQ(findEntry(all, "zzz", cand), nullptr);
    EXPECT_TRUE(cand.empty());
    EXPECT_EQ(findEntry(all, "", cand), nullptr);

    // Two sessions with the same name (on the disc several are called "Intro"): neither is
    // picked; both are listed with their KUIDs, which then pick one.
    const CatalogEntry i1{Kuid{523, 1022, 1}, "Intro", {}};
    const CatalogEntry i2{Kuid{523, 1015, 1}, "Intro", {}};
    const std::vector<const CatalogEntry*> intros = {&i1, &i2, &a};
    EXPECT_EQ(findEntry(intros, "intro", cand), nullptr);
    EXPECT_EQ(cand, (std::vector<std::string>{"Intro  <kuid2:523:1022:1>", "Intro  <kuid2:523:1015:1>"}));
    EXPECT_EQ(findEntry(intros, "<kuid2:523:1015:1>", cand), &i2);
    // Two installed versions of one KUID: the exact version picks; another version alone
    // does not when both exist.
    const CatalogEntry v1{Kuid{9, 9, 1}, "Route v1", {}};
    const CatalogEntry v2{Kuid{9, 9, 2}, "Route v2", {}};
    const std::vector<const CatalogEntry*> versions = {&v1, &v2};
    EXPECT_EQ(findEntry(versions, "<kuid2:9:9:2>", cand), &v2);
    EXPECT_EQ(findEntry(versions, "<kuid2:9:9:7>", cand), nullptr);
    EXPECT_EQ(cand.size(), 2U);
    const std::vector<const CatalogEntry*> one = {&v1};
    EXPECT_EQ(findEntry(one, "<kuid2:9:9:7>", cand), &v1);  // the only installed version
}

TEST(FlyCamera, ControlsFromKeysMouseAndSticks) {
    FlyCamera cam;  // at the origin, heading +x, level
    FlyControls none;
    EXPECT_FALSE(applyFlyControls(cam, none, 0.1));

    FlyControls go;
    go.forward = 1;
    EXPECT_TRUE(applyFlyControls(cam, go, 0.5));
    EXPECT_NEAR(cam.position.x, 15, 1e-9);  // 30 m/s for half a second
    go.fast = true;
    applyFlyControls(cam, go, 0.1);
    EXPECT_NEAR(cam.position.x, 30, 1e-9);  // 5x faster
    go.forward = 7;  // key + stick sums are clamped to full speed
    go.fast = false;
    applyFlyControls(cam, go, 0.1);
    EXPECT_NEAR(cam.position.x, 33, 1e-9);
    applyFlyControls(cam, go, 30);  // a long pause counts as half a second at most
    EXPECT_NEAR(cam.position.x, 48, 1e-9);

    FlyControls wheel;
    wheel.wheel = 2;  // wheel steps move without time
    applyFlyControls(cam, wheel, 0);
    EXPECT_NEAR(cam.position.x, 68, 1e-9);

    FlyControls mouse;
    mouse.mouseDx = 100;  // dragging right turns right (clockwise)
    mouse.mouseDy = -50;  // dragging up looks up
    applyFlyControls(cam, mouse, 0);
    EXPECT_NEAR(cam.heading, -0.4, 1e-9);
    EXPECT_NEAR(cam.pitch, 0.2, 1e-9);

    FlyControls bad;
    bad.forward = std::numeric_limits<double>::quiet_NaN();
    bad.mouseDx = std::numeric_limits<double>::infinity();
    const auto before = cam.position;
    EXPECT_FALSE(applyFlyControls(cam, bad, 0.1));
    EXPECT_DOUBLE_EQ(cam.position.x, before.x);
}

TEST(FlyCamera, StickDeadZone) {
    EXPECT_DOUBLE_EQ(stickAxis(0), 0);
    EXPECT_DOUBLE_EQ(stickAxis(3000), 0);  // inside the 15% dead zone
    EXPECT_DOUBLE_EQ(stickAxis(-3000), 0);
    EXPECT_NEAR(stickAxis(32767), 1, 1e-12);
    EXPECT_NEAR(stickAxis(-32768), -1, 1e-12);  // clamped
    EXPECT_NEAR(stickAxis(static_cast<int>(32767 * 0.575)), 0.5, 1e-4);  // halfway between the dead zone and the edge
    EXPECT_DOUBLE_EQ(stickAxis(20000, 0.99), 0);  // dead zone held below 0.95: 20000 is ~0.61
    EXPECT_GT(stickAxis(32767, std::numeric_limits<double>::quiet_NaN()), 0.99);
}
