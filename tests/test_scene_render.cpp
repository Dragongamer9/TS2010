#include "content/AssetConfig.h"
#include "view/AssetMeshes.h"
#include "view/SceneRender.h"

#include <gtest/gtest.h>

#include <cmath>
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

TEST(SceneRender, MeshSelectionSkipsLodsNightAttachmentsAndShadows) {
    std::map<std::string, std::size_t> parts;
    const auto picked = selectAssetMeshes(config(
        "mesh-table\n{\n"
        "  lod-0\n  {\n    mesh \"lod0.im\"\n    lod-level 0\n    auto-create 1\n  }\n"
        "  lod-1\n  {\n    mesh \"lod1.im\"\n    lod-level 1\n    auto-create 1\n  }\n"
        "  default-night\n  {\n    mesh \"night/night.im\"\n    night-mesh-base \"lod-0\"\n  }\n"
        "  door\n  {\n    mesh \"door.im\"\n    att \"a.door\"\n    att-parent \"lod-0\"\n  }\n"
        "  shadow\n  {\n    mesh \"car_shadow.im\"\n  }\n"
        "  load\n  {\n    mesh \"load/load.im\"\n    auto-create 1\n  }\n"
        "}\n"),
        parts);
    EXPECT_EQ(picked, (std::vector<std::string>{"lod0.im", "load/load.im"}));
    EXPECT_EQ(parts["mesh-table entries skipped: lower level of detail"], 1U);
    EXPECT_EQ(parts["mesh-table entries skipped: night variant"], 1U);
    EXPECT_EQ(parts["mesh-table entries skipped: on an attachment point"], 1U);
    EXPECT_EQ(parts["mesh-table entries skipped: shadow mesh"], 1U);
}

TEST(SceneRender, MeshSelectionTopLevelAndAssetFilename) {
    std::map<std::string, std::size_t> parts;
    EXPECT_EQ(selectAssetMeshes(config("mesh \"tree.im\"\n"), parts), std::vector<std::string>{"tree.im"});
    EXPECT_EQ(selectAssetMeshes(config("kind \"scenery\"\nasset-filename \"usa_building_brick_5\"\n"), parts),
              std::vector<std::string>{"usa_building_brick_5.im"});
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
    EXPECT_TRUE(wantsAlphaTest(image(40, 60, 0)));    // leaves: a clear/solid mix
    EXPECT_FALSE(wantsAlphaTest(image(0, 100, 0)));   // all solid
    EXPECT_FALSE(wantsAlphaTest(image(2, 98, 0)));    // a few clear texels only
    EXPECT_FALSE(wantsAlphaTest(image(98, 2, 0)));    // nearly all clear (e.g. a gloss mask)
    EXPECT_FALSE(wantsAlphaTest(image(30, 30, 40)));  // smooth alpha: not a cut-out
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
