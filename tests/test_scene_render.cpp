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
