#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/GroundFile.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace trainz::content;
using namespace trainz::test;

namespace {

struct Sec {
    const char* tag;  // as read, e.g. "Secs"
    int x, y;
    Bytes data;
};

void putF32(Bytes& b, float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    putU32(b, u);
}

constexpr std::size_t kHeader = 8;
constexpr std::size_t kEntry = 20;

// Builds a mapfile.gnd laid out like the disc ones (version 13):
// "GND\r", count, then {x, y, reversed tag, start, end} per section.
Bytes gnd(const std::vector<Sec>& secs, std::size_t extraHeader = 12) {
    Bytes b = bytesOf("GND\r");
    putU32(b, static_cast<std::uint32_t>(secs.size()));
    const std::size_t indexAt = b.size();
    b.resize(b.size() + kEntry * secs.size(), 0);
    b.resize(b.size() + extraHeader, 0xEE);
    for (std::size_t i = 0; i < secs.size(); ++i) {
        const std::uint32_t start = static_cast<std::uint32_t>(b.size());
        b.insert(b.end(), secs[i].data.begin(), secs[i].data.end());
        const std::uint32_t end = static_cast<std::uint32_t>(b.size());
        for (std::size_t j = 0; j < 4096; ++j) {
            putU16(b, static_cast<std::uint16_t>(j + i));
        }
        const std::size_t e = indexAt + kEntry * i;
        setU32(b, e, static_cast<std::uint32_t>(secs[i].x));
        setU32(b, e + 4, static_cast<std::uint32_t>(secs[i].y));
        for (int k = 0; k < 4; ++k) {
            b[e + 8 + k] = static_cast<std::uint8_t>(secs[i].tag[3 - k]);
        }
        setU32(b, e + 12, start);
        setU32(b, e + 16, end);
    }
    return b;
}

Bytes filler(std::size_t n, std::uint8_t v) { return Bytes(n, v); }

// Version-13 section data: 250 texture slots (slot 0 used, KUID <kuid:-3:1234>,
// slot 1 used, <kuid2:523:99:2>), two height cells, then vertices.
Bytes sectionData(const std::string& tag) {
    Bytes b;
    putU32(b, 250);
    putF32(b, 1.5f);
    putU32(b, 1234);                 // content
    putU32(b, 0xFFFFFFFDu);          // user -3, version bits all ones (= plain kuid)
    putF32(b, 0.25f);
    putU32(b, 99);
    putU32(b, 523u | (2u << 25));    // user 523, version 2
    for (int i = 2; i < 250; ++i) {
        putF32(b, 0.0f);
    }
    putU32(b, 2);
    putU16(b, 3), putU16(b, 4), putF32(b, 7.0f);
    putU16(b, 40), putU16(b, 0), putF32(b, 1.0f);  // out of range: the game ignores it
    const std::size_t verts = groundVertexSide(tag) * groundVertexSide(tag);
    // vertex 0: 0xFE short form; vertex 1: 0xFD with height; vertex 2: list of 2 layers;
    // vertex 3: list of 4 layers (only 3 kept); vertex 4: empty list; the rest 0xFE.
    b.push_back(0xFE), b.push_back(5), b.push_back(9);
    b.push_back(0xFD), b.push_back(6), b.push_back(0), putF32(b, 12.5f);
    for (std::uint8_t x : {1, 200, 7, 0, 55, 8, 0xFF}) {
        b.push_back(x);
    }
    putF32(b, -2.0f);
    for (std::uint8_t x : {1, 10, 0, 2, 20, 0, 3, 30, 0, 4, 40, 0, 0xFF}) {
        b.push_back(x);
    }
    putF32(b, 3.0f);
    b.push_back(0xFF);
    putF32(b, 4.0f);
    for (std::size_t i = 5; i < verts; ++i) {
        b.push_back(0xFE), b.push_back(0), b.push_back(0);
    }
    return b;
}

}  // namespace

TEST(Ground, ParsesIndexAndOverviews) {
    const auto g = parseGround(gnd({{"Sec5", -1, 0, filler(100, 1)}, {"Secs", -3, 1, filler(40, 2)}}));
    EXPECT_EQ(g.version, 13);
    ASSERT_EQ(g.sections.size(), 2u);
    EXPECT_EQ(g.sections[0].tag, "Sec5");
    EXPECT_EQ(g.sections[0].x, -1);
    EXPECT_EQ(g.sections[0].y, 0);
    EXPECT_EQ(g.sections[1].x, -3);
    EXPECT_EQ(g.sections[1].y, 1);
    EXPECT_EQ(g.sections[0].data.size(), 100u);
    EXPECT_EQ(g.sections[1].data.at(0), 2);
    ASSERT_EQ(g.sections[1].overview.size(), 4096u);
    EXPECT_EQ(g.sections[1].overview[5], 6);
    EXPECT_EQ(g.extraHeader.size(), 12u);
    EXPECT_TRUE(g.warnings.empty());
}

TEST(Ground, DecodesSectionData) {
    for (const char* tag : {"Secs", "Sec5"}) {
        const auto g = parseGround(gnd({{tag, 0, 0, sectionData(tag)}}));
        const auto d = parseGroundSection(g.sections[0], g.version);
        EXPECT_EQ(d.textureSlotCount, 250u);
        ASSERT_EQ(d.textures.size(), 250u);
        EXPECT_TRUE(d.textures[0].used);
        EXPECT_FLOAT_EQ(d.textures[0].weight, 1.5f);
        EXPECT_EQ(formatKuid(d.textures[0].kuid.normalized()), "<kuid:-3:1234>");
        EXPECT_EQ(formatKuid(d.textures[1].kuid.normalized()), "<kuid2:523:99:2>");
        EXPECT_FALSE(d.textures[2].used);
        ASSERT_EQ(d.cells.size(), 2u);
        EXPECT_EQ(d.cells[0].col, 3);
        EXPECT_EQ(d.cells[0].row, 4);
        EXPECT_FLOAT_EQ(d.cells[0].value, 7.0f);
        const std::size_t side = groundVertexSide(tag);
        ASSERT_EQ(d.vertices.size(), side * side);
        const auto& v = d.vertices;
        EXPECT_EQ(v[0].texture[0], 5);
        EXPECT_EQ(v[0].weight[0], 255);
        EXPECT_EQ(v[0].extra[0], 9);
        EXPECT_EQ(v[0].texture[1], 0xFF);
        EXPECT_FLOAT_EQ(v[0].height, 0.0f);
        EXPECT_FLOAT_EQ(v[1].height, 12.5f);
        EXPECT_EQ(v[2].layersInFile, 2);
        EXPECT_EQ(v[2].texture[1], 0);
        EXPECT_EQ(v[2].weight[1], 55);
        EXPECT_EQ(v[2].extra[1], 8);
        EXPECT_EQ(v[2].texture[2], 0xFF);
        EXPECT_FLOAT_EQ(v[2].height, -2.0f);
        EXPECT_EQ(v[3].layersInFile, 4);
        EXPECT_EQ(v[3].texture[2], 3);
        EXPECT_FLOAT_EQ(v[3].height, 3.0f);
        EXPECT_EQ(v[4].layersInFile, 0);
        EXPECT_FLOAT_EQ(v[4].height, 4.0f);
    }
    EXPECT_EQ(groundVertexSide("Secs"), 76u);
    EXPECT_EQ(groundVertexSide("Sec5"), 148u);
}

TEST(Ground, SectionDataMustEndExactly) {
    Bytes data = sectionData("Secs");
    data.push_back(0);  // one byte too many
    auto g = parseGround(gnd({{"Secs", 0, 0, data}}));
    EXPECT_THROW(parseGroundSection(g.sections[0], g.version), ContentError);
    data.resize(data.size() - 2);  // one byte too few
    g = parseGround(gnd({{"Secs", 0, 0, data}}));
    EXPECT_THROW(parseGroundSection(g.sections[0], g.version), ContentError);
    // A Secs body is too short for a Sec5 tag.
    g = parseGround(gnd({{"Sec5", 0, 0, sectionData("Secs")}}));
    EXPECT_THROW(parseGroundSection(g.sections[0], g.version), ContentError);
}

TEST(Ground, TooManyHeightCellsReadAsNone) {
    // 0x82c038: a count above 36 * 36 becomes 0 and no cells are read.
    Bytes b;
    putU32(b, 0);  // no texture slots
    putU32(b, 36 * 36 + 1);
    for (std::size_t i = 0; i < 76 * 76; ++i) {
        b.push_back(0xFE), b.push_back(0), b.push_back(0);
    }
    const auto g = parseGround(gnd({{"Secs", 0, 0, b}}));
    const auto d = parseGroundSection(g.sections[0], g.version);
    EXPECT_TRUE(d.textures.empty());
    EXPECT_TRUE(d.cells.empty());
}

TEST(Ground, DuplicatePositionsAreWarnedAbout) {
    const auto g = parseGround(
        gnd({{"Sec5", -1, 0, filler(10, 1)}, {"Secs", -1, 0, filler(10, 2)}, {"Secs", 0, 0, filler(10, 3)}}));
    ASSERT_EQ(g.warnings.size(), 1u);
    EXPECT_NE(g.warnings[0].find("both at (-1, 0)"), std::string::npos);
}

TEST(Ground, Errors) {
    EXPECT_THROW(parseGround(bytesOf("GND")), ContentError);
    Bytes b = gnd({{"Secs", 0, 0, filler(10, 1)}});
    b[0] = 'X';
    EXPECT_THROW(parseGround(b), ContentError);
    b = gnd({{"Secs", 0, 0, filler(10, 1)}});
    b[3] = 11;  // older version: overview map size not worked out
    EXPECT_THROW(parseGround(b), ContentError);
    b = gnd({{"Secs", 0, 0, filler(10, 1)}});
    setU32(b, 4, 50000);  // more sections than the file can hold
    EXPECT_THROW(parseGround(b), ContentError);
    b = gnd({{"Secs", 0, 0, filler(10, 1)}});
    setU32(b, kHeader + 16, static_cast<std::uint32_t>(b.size()));  // end leaves no room for the overview
    EXPECT_THROW(parseGround(b), ContentError);
    b = gnd({{"Secs", 0, 0, filler(10, 1)}});
    b[kHeader + 8] = 'Z';  // unknown tag
    EXPECT_THROW(parseGround(b), ContentError);
    b = gnd({{"Secs", 0, 0, filler(10, 1)}, {"Secs", 1, 0, filler(10, 2)}});
    setU32(b, kHeader + kEntry + 12, kHeader + 2 * kEntry + 12 + 5);  // second section starts inside the first
    EXPECT_THROW(parseGround(b), ContentError);
}

TEST(Ground, EveryTruncationAndByteChangeIsSafe) {
    const Bytes good = gnd({{"Secs", 0, 0, sectionData("Secs")}}, 4);
    for (std::size_t len = 0; len < good.size(); len += 97) {
        try {
            const auto g = parseGround(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len)));
            (void)parseGroundSection(g.sections[0], g.version);
        } catch (const ContentError&) {
        }
    }
    const std::size_t dataAt = kHeader + kEntry + 4;
    for (std::size_t i = 0; i < dataAt + 2100; i += (i < dataAt + 8 ? 1 : 13)) {
        for (std::uint8_t v : {std::uint8_t{0}, std::uint8_t{0xFF}, std::uint8_t{0xFD}, std::uint8_t{0x10}}) {
            Bytes b = good;
            b[i] = v;
            try {
                const auto g = parseGround(b);
                (void)parseGroundSection(g.sections[0], g.version);
            } catch (const ContentError&) {
            }
        }
    }
}

TEST(Ground, OverviewColourIsBgr565) {
    std::uint8_t r, g, b;
    overviewToRgb(0x001F, r, g, b);  // low 5 bits = red under the observed BGR reading
    EXPECT_EQ(r, 255);
    EXPECT_EQ(g, 0);
    EXPECT_EQ(b, 0);
    overviewToRgb(0xF800, r, g, b);
    EXPECT_EQ(b, 255);
}
