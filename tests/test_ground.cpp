#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/GroundFile.h"

#include <gtest/gtest.h>

using namespace trainz::content;
using namespace trainz::test;

namespace {

struct Sec {
    const char* tag;  // as read, e.g. "Secs"
    int x, y;
    std::size_t dataSize;
};

// Builds a mapfile.gnd laid out like the one on the disc.
Bytes gnd(const std::vector<Sec>& secs, std::size_t extraHeader = 12) {
    Bytes b = bytesOf("GND\r");
    putU32(b, static_cast<std::uint32_t>(secs.size()));
    putU32(b, 0);
    putU32(b, 4);
    const std::size_t indexAt = b.size();
    b.resize(b.size() + 20 * secs.size(), 0);
    b.resize(b.size() + extraHeader, 0xEE);
    for (std::size_t i = 0; i < secs.size(); ++i) {
        const std::uint32_t start = static_cast<std::uint32_t>(b.size());
        b.resize(b.size() + secs[i].dataSize, static_cast<std::uint8_t>(i + 1));
        const std::uint32_t end = static_cast<std::uint32_t>(b.size());
        for (std::size_t j = 0; j < 4096; ++j) {
            putU16(b, static_cast<std::uint16_t>(j + i));
        }
        std::uint8_t* e = b.data() + indexAt + 20 * i;
        e[0] = static_cast<std::uint8_t>(secs[i].tag[3]), e[1] = static_cast<std::uint8_t>(secs[i].tag[2]);
        e[2] = static_cast<std::uint8_t>(secs[i].tag[1]), e[3] = static_cast<std::uint8_t>(secs[i].tag[0]);
        setU32(b, indexAt + 20 * i + 4, start);
        setU32(b, indexAt + 20 * i + 8, end);
        setU32(b, indexAt + 20 * i + 12, static_cast<std::uint32_t>(secs[i].x));
        setU32(b, indexAt + 20 * i + 16, static_cast<std::uint32_t>(secs[i].y));
    }
    return b;
}

}  // namespace

TEST(Ground, ParsesSectionsAndOverviews) {
    const auto g = parseGround(gnd({{"Sec5", -1, 0, 100}, {"Secs", -3, 1, 40}}));
    ASSERT_EQ(g.sections.size(), 2u);
    EXPECT_EQ(g.sections[0].tag, "Sec5");
    EXPECT_EQ(g.sections[0].x, -1);
    EXPECT_EQ(g.sections[1].y, 1);
    EXPECT_EQ(g.sections[0].data.size(), 100u);
    EXPECT_EQ(g.sections[1].data.at(0), 2);
    ASSERT_EQ(g.sections[1].overview.size(), 4096u);
    EXPECT_EQ(g.sections[1].overview[5], 6);
    EXPECT_EQ(g.extraHeader.size(), 12u);
    EXPECT_EQ(g.headerWord3, 4u);
    EXPECT_TRUE(g.warnings.empty());
}

TEST(Ground, Errors) {
    EXPECT_THROW(parseGround(bytesOf("GND")), ContentError);
    Bytes b = gnd({{"Secs", 0, 0, 10}});
    b[0] = 'X';
    EXPECT_THROW(parseGround(b), ContentError);
    b = gnd({{"Secs", 0, 0, 10}});
    setU32(b, 4, 50000);  // more sections than the file can hold
    EXPECT_THROW(parseGround(b), ContentError);
    b = gnd({{"Secs", 0, 0, 10}});
    setU32(b, 16 + 8, static_cast<std::uint32_t>(b.size()));  // end leaves no room for the overview
    EXPECT_THROW(parseGround(b), ContentError);
    b = gnd({{"Secs", 0, 0, 10}});
    b[16] = 'Z';  // unknown tag
    EXPECT_THROW(parseGround(b), ContentError);
    b = gnd({{"Secs", 0, 0, 10}, {"Secs", 1, 0, 10}});
    setU32(b, 16 + 20 + 4, 16 + 40 + 12 + 5);  // second section starts inside the first
    EXPECT_THROW(parseGround(b), ContentError);
}

TEST(Ground, EveryTruncationAndByteChangeIsSafe) {
    const Bytes good = gnd({{"Sec5", 0, 0, 8}, {"Secs", 1, 0, 4}}, 4);
    for (std::size_t len = 0; len < good.size(); len += 97) {
        try {
            (void)parseGround(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len)));
        } catch (const ContentError&) {
        }
    }
    for (std::size_t i = 0; i < 16 + 40 + 4 + 8; ++i) {  // header, index and first section
        for (std::uint8_t v : {std::uint8_t{0}, std::uint8_t{0xFF}, std::uint8_t{0x10}}) {
            Bytes b = good;
            b[i] = v;
            try {
                (void)parseGround(b);
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
