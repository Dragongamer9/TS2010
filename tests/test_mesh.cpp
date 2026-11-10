#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/MeshFile.h"
#include "view/MeshPreview.h"

#include <gtest/gtest.h>

#include <cstring>
#include <limits>

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
Bytes padded(const std::string& s, std::size_t n) {
    Bytes b(s.begin(), s.end());
    b.resize(n, 0);
    return b;
}

// A quad (2 triangles) like the smallest .im in Builtin\8.ja.
Bytes geom(std::uint32_t version, std::uint32_t flags = 0, const std::string& bone = "") {
    Bytes b;
    putU32(b, version);
    putU32(b, 4);
    if (version == 201) {
        putU32(b, flags);
    }
    putF(b, 3.3f);
    putU32(b, 4);  // vertices
    putU32(b, 2);  // triangles
    putU32(b, 6);  // indices
    putU32(b, 2);  // face normals
    putU32(b, 0);
    const std::size_t nameBlock = bone.empty() ? 0 : (bone.size() + 4) / 4 * 4;
    if (version != 102) {
        putU32(b, static_cast<std::uint32_t>(nameBlock));
        const Bytes n = padded(bone, nameBlock);
        b.insert(b.end(), n.begin(), n.end());
    }
    const float vs[4][5] = {{-1, 1, 0, 0, 0}, {-1, -1, 0, 0, 1}, {1, 1, 0, 1, 0}, {1, -1, 0, 1, 1}};
    for (const auto& v : vs) {
        for (float f : v) {
            putF(b, f);
        }
    }
    for (std::uint16_t i : {0, 1, 2, 3, 2, 1}) {
        putU16(b, i);
    }
    for (int i = 0; i < 6; ++i) {  // 4 vertex normals + 2 face normals
        putF(b, 0), putF(b, 0), putF(b, 1);
    }
    if (flags & 1u) {
        for (int i = 0; i < 4; ++i) {
            putF(b, 0.5f), putF(b, 0.25f), putF(b, 0.125f);
        }
    }
    return b;
}

Bytes matl(std::uint32_t version, const std::string& name, const std::vector<std::pair<std::uint32_t, std::string>>& tex) {
    Bytes b;
    putU32(b, version);
    const std::size_t nl = (name.size() + 4) / 4 * 4;
    putU32(b, static_cast<std::uint32_t>(nl));
    const Bytes n = padded(name, nl);
    b.insert(b.end(), n.begin(), n.end());
    for (int i = 0; i < 16; ++i) {
        putF(b, i == 15 ? 10.0f : 1.0f);
    }
    putU32(b, static_cast<std::uint32_t>(tex.size()));
    for (const auto& [type, path] : tex) {
        putU32(b, type);
        const std::size_t pl = (path.size() + 4) / 4 * 4;
        putU32(b, static_cast<std::uint32_t>(pl));
        const Bytes p = padded(path, pl);
        b.insert(b.end(), p.begin(), p.end());
        putF(b, 1.0f);
    }
    return b;
}

Bytes mesh(const std::vector<std::pair<Bytes, Bytes>>& parts, bool withInfl = true) {
    Bytes body;
    putTag(body, "IDXM");
    Bytes info;
    putU32(info, 102);
    info.resize(44, 0);
    const Bytes ic = chunk("INFO", info);
    body.insert(body.end(), ic.begin(), ic.end());
    std::uint32_t idx = 0;
    for (const auto& [m, g] : parts) {
        Bytes c;
        putU32(c, 100);
        putU32(c, idx++);
        const Bytes mc = chunk("MATL", m), gc = chunk("GEOM", g);
        c.insert(c.end(), mc.begin(), mc.end());
        c.insert(c.end(), gc.begin(), gc.end());
        const Bytes cc = chunk("CHNK", c);
        body.insert(body.end(), cc.begin(), cc.end());
    }
    if (withInfl) {
        const Bytes infl = chunk("INFL", Bytes{100, 0, 0, 0, 0, 0, 0, 0});
        body.insert(body.end(), infl.begin(), infl.end());
    }
    Bytes b;
    putTag(b, "JIRF");
    putU32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

}  // namespace

TEST(Mesh, Geometry102) {
    const auto g = parseGeometryChunk(geom(102));
    EXPECT_EQ(g.version, 102u);
    ASSERT_EQ(g.vertices.size(), 4u);
    EXPECT_FLOAT_EQ(g.vertices[1].y, -1.0f);
    EXPECT_FLOAT_EQ(g.vertices[3].v, 1.0f);
    EXPECT_EQ(g.indices, (std::vector<std::uint16_t>{0, 1, 2, 3, 2, 1}));
    EXPECT_EQ(g.vertexNormals.size(), 4u);
    EXPECT_EQ(g.faceNormals.size(), 2u);
    EXPECT_TRUE(g.boneName.empty());
}

TEST(Mesh, Geometry200WithBoneName) {
    const auto g = parseGeometryChunk(geom(200, 0, "b.r.door1"));
    EXPECT_EQ(g.boneName, "b.r.door1");
    EXPECT_EQ(g.vertices.size(), 4u);
    EXPECT_FLOAT_EQ(g.vertexNormals[2][2], 1.0f);
}

TEST(Mesh, Geometry201Flag1HasExtraValuesLast) {
    const auto g = parseGeometryChunk(geom(201, 1));
    EXPECT_EQ(g.flags, 1u);
    ASSERT_EQ(g.extraPerVertex.size(), 4u);
    EXPECT_FLOAT_EQ(g.extraPerVertex[3][1], 0.25f);
    EXPECT_FLOAT_EQ(g.faceNormals[1][2], 1.0f);
}

TEST(Mesh, GeometryErrors) {
    Bytes g = geom(200);
    g.pop_back();
    EXPECT_THROW(parseGeometryChunk(g), ContentError);  // size does not fit the counts
    g = geom(200);
    const std::size_t firstIndex = 36 + 4 * 20;
    g[firstIndex] = 9;  // index 9 >= 4 vertices
    EXPECT_THROW(parseGeometryChunk(g), ContentError);
    g = geom(200);
    setU32(g, 20, 7);  // indices != 3 * triangles
    EXPECT_THROW(parseGeometryChunk(g), ContentError);
    g = geom(200);
    setU32(g, 0, 300);  // unknown version
    EXPECT_THROW(parseGeometryChunk(g), ContentError);
    g = geom(201);
    setU32(g, 8, 2);  // unknown flag
    EXPECT_THROW(parseGeometryChunk(g), ContentError);
    g = geom(200);
    setU32(g, 12, 70000);  // more vertices than 16-bit indices can reach
    EXPECT_THROW(parseGeometryChunk(g), ContentError);
}

TEST(Mesh, MaterialWithTextures) {
    const auto m = parseMaterialChunk(matl(103, "wall.m.tbumptex",
                                           {{1, "C:\\Trainz Content\\Bridge\\wall.texture"},
                                            {8, "C:\\Trainz Content\\Bridge\\wall-normal.texture"}}));
    EXPECT_EQ(m.version, 103u);
    EXPECT_EQ(m.name, "wall.m.tbumptex");
    EXPECT_EQ(m.kind, "m.tbumptex");
    ASSERT_EQ(m.textures.size(), 2u);
    EXPECT_EQ(m.textures[0].type, 1u);
    EXPECT_EQ(m.textures[0].fileName, "wall.texture");
    EXPECT_EQ(m.textures[1].fileName, "wall-normal.texture");
    EXPECT_FLOAT_EQ(m.values[15], 10.0f);
}

TEST(Mesh, MaterialKindFromStarredName) {
    EXPECT_EQ(parseMaterialChunk(matl(102, "*shed*6A392CC2*floor*m.onetex", {})).kind, "m.onetex");
    EXPECT_EQ(parseMaterialChunk(matl(102, "plain", {})).kind, "");
}

TEST(Mesh, MaterialErrors) {
    Bytes m = matl(102, "x.m.onetex", {{1, "a.texture"}});
    m.push_back(0);
    EXPECT_THROW(parseMaterialChunk(m), ContentError);  // unexplained bytes
    m = matl(102, "x.m.onetex", {{1, "a.texture"}});
    m.resize(m.size() - 2);
    EXPECT_THROW(parseMaterialChunk(m), ContentError);
    m = matl(104, "x", {});
    EXPECT_THROW(parseMaterialChunk(m), ContentError);
}

TEST(Mesh, WholeFile) {
    const auto f = parseMesh(mesh({{matl(102, "*a*1*one*m.onetex", {{1, "D:\\x\\one.texture"}}), geom(200)},
                                   {matl(103, "two.m.notex", {}), geom(201, 1, "b.r.load")}}));
    ASSERT_EQ(f.parts.size(), 2u);
    EXPECT_EQ(f.parts[0].index, 0u);
    EXPECT_EQ(f.parts[1].index, 1u);
    EXPECT_EQ(f.parts[0].material.textures.at(0).fileName, "one.texture");
    EXPECT_EQ(f.parts[1].geometry.boneName, "b.r.load");
    EXPECT_EQ(f.otherChunks, (std::vector<std::string>{"INFL"}));
    EXPECT_EQ(f.infoWords.at(0), 102u);
}

TEST(Mesh, EveryTruncationAndByteChangeIsSafe) {
    const Bytes good = mesh({{matl(102, "*a*1*one*m.onetex", {{1, "one.texture"}}), geom(200, 0, "b")}});
    auto tryParse = [](const Bytes& b) {
        try {
            const auto m = parseMesh(b);
            (void)trainz::view::renderMeshPreview(m, 32, 24, trainz::view::PreviewView::ThreeQuarter);
        } catch (const ContentError&) {
        }
    };
    for (std::size_t len = 0; len < good.size(); ++len) {
        tryParse(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len)));
    }
    for (std::size_t i = 0; i < good.size(); ++i) {
        for (std::uint8_t v : {std::uint8_t{0}, std::uint8_t{0xFF}, std::uint8_t{0x7F}}) {
            Bytes b = good;
            b[i] = v;
            tryParse(b);
        }
    }
}

TEST(Preview, DrawsSomething) {
    const auto f = parseMesh(mesh({{matl(102, "a", {}), geom(200)}}));
    for (auto view : {trainz::view::PreviewView::Top, trainz::view::PreviewView::ThreeQuarter}) {
        const auto img = trainz::view::renderMeshPreview(f, 64, 48, view);
        ASSERT_EQ(img.size(), 64u * 48 * 4);
        std::size_t lit = 0;
        for (std::size_t i = 0; i < img.size(); i += 4) {
            lit += img[i] != 40;
        }
        EXPECT_GT(lit, 64u * 48 / 8);  // the quad covers a good part of the image
    }
}

TEST(Preview, ExtremeFiniteCoordinatesDoNotBreakTheImage) {
    auto f = parseMesh(mesh({{matl(102, "a", {}), geom(200)}}));
    for (auto& v : f.parts[0].geometry.vertices) {
        v.x *= 3.0e38f;  // finite, but differences overflow to infinity
        v.y *= 3.0e38f;
    }
    const auto img = trainz::view::renderMeshPreview(f, 32, 32, trainz::view::PreviewView::Top);
    EXPECT_EQ(img.size(), 32u * 32 * 4);
}

TEST(Preview, HugeAndBrokenCoordinatesAreSafe) {
    auto f = parseMesh(mesh({{matl(102, "a", {}), geom(200)}}));
    f.parts[0].geometry.vertices[0].x = 3.0e38f;
    f.parts[0].geometry.vertices[1].y = std::numeric_limits<float>::quiet_NaN();
    f.parts[0].geometry.vertices[2].z = -std::numeric_limits<float>::infinity();
    EXPECT_NO_THROW((void)trainz::view::renderMeshPreview(f, 32, 32, trainz::view::PreviewView::Side));
}
