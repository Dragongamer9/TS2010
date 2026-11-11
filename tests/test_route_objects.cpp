#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/ObjectContainer.h"

#include <gtest/gtest.h>

using namespace trainz::content;
using namespace trainz::test;

namespace {

// One item: index, tag (file byte order, i.e. reversed), KUID, size, payload.
void item(Bytes& b, std::int32_t index, const char* fileTag, std::int32_t content, std::uint32_t packedUser,
          const Bytes& payload) {
    putU32(b, static_cast<std::uint32_t>(index));
    b.insert(b.end(), fileTag, fileTag + 4);
    putU32(b, static_cast<std::uint32_t>(content));
    putU32(b, packedUser);
    putU32(b, static_cast<std::uint32_t>(payload.size()));
    b.insert(b.end(), payload.begin(), payload.end());
}

void endContainer(Bytes& b, std::uint32_t version, std::uint32_t word) {
    putU32(b, 0xFFFFFFFFu);  // index -1 ends the items
    // The 16-byte tail every disc file carries: FEEDCEEB, 5, word, -1.
    putU32(b, 16);
    putU32(b, 0xFEEDCEEBu);
    putU32(b, version);
    putU32(b, word);
    putU32(b, 0xFFFFFFFFu);
}

// Shaped like a disc mapfile.trk: u32 2, a stretch container, a vertex container.
Bytes sampleTrk() {
    Bytes b;
    putU32(b, 2);
    putU32(b, 5);
    putU32(b, 2);
    item(b, 0, "tSkt", 1116, 523, Bytes(30, 0x11));
    item(b, 1, "tSkt", 1116, 523, Bytes(30, 0x22));
    endContainer(b, 5, 2);
    putU32(b, 5);
    putU32(b, 2);
    item(b, 0, "xVkt", -1, 0x1FFFFFFu, Bytes(41, 0x33));
    endContainer(b, 5, 2);
    return b;
}

}  // namespace

TEST(RouteObjects, ReadsContainersToTheEnd) {
    const auto f = parseRouteObjectFile(sampleTrk());
    EXPECT_EQ(f.fileWord, 2U);
    ASSERT_EQ(f.containers.size(), 2U);
    const auto& stretches = f.containers[0];
    EXPECT_EQ(stretches.version, 5U);
    EXPECT_EQ(stretches.word, 2U);
    ASSERT_EQ(stretches.items.size(), 2U);
    EXPECT_EQ(stretches.items[1].index, 1);
    EXPECT_EQ(stretches.items[0].tag, "tkSt");  // readable order
    EXPECT_EQ(stretches.items[0].kuid.user, 523);
    EXPECT_EQ(stretches.items[0].kuid.content, 1116);
    EXPECT_EQ(stretches.items[1].payload, Bytes(30, 0x22));
    EXPECT_EQ(stretches.tail.size(), 16U);
    EXPECT_EQ(f.containers[1].items.at(0).tag, "tkVx");
    EXPECT_EQ(f.containers[1].items.at(0).payload.size(), 41U);
}

TEST(RouteObjects, ClassNamesFromTheRegistry) {
    EXPECT_EQ(objectClassName("mOBJ"), "mapobject");
    EXPECT_EQ(objectClassName("mobs"), "mapobjectstub");
    EXPECT_EQ(objectClassName("mIND"), "industry");
    EXPECT_EQ(objectClassName("sgOb"), "mosignal");
    EXPECT_EQ(objectClassName("BHVR"), "behavior");
    EXPECT_EQ(objectClassName("tkSt"), "");  // registered without a name next to it
}

TEST(RouteObjects, BadContainersAreRefused) {
    Bytes v2;
    putU32(v2, 9);
    putU32(v2, 2);  // version 2: items without sizes
    putU32(v2, 0);
    EXPECT_THROW(parseRouteObjectFile(v2), ContentError);

    Bytes v16;
    putU32(v16, 2);
    putU32(v16, 16);
    EXPECT_THROW(parseRouteObjectFile(v16), ContentError);

    Bytes huge;
    putU32(huge, 2);
    putU32(huge, 5);
    putU32(huge, 2);
    putU32(huge, 0);
    huge.insert(huge.end(), {'t', 'S', 'k', 't'});
    putU32(huge, 1);
    putU32(huge, 1);
    putU32(huge, 0x1000000u);  // one byte over the game's limit
    EXPECT_THROW(parseRouteObjectFile(huge), ContentError);

    EXPECT_THROW(parseRouteObjectFile(Bytes{1, 2}), ContentError);
}

TEST(RouteObjects, EveryTruncationAndByteChangeIsSafe) {
    const Bytes good = sampleTrk();
    std::size_t accepted = 0;
    for (std::size_t n = 0; n < good.size(); ++n) {
        try {
            // Cut exactly between containers, the shorter file is still well formed.
            const auto f = parseRouteObjectFile(std::span(good.data(), n));
            EXPECT_LT(f.containers.size(), 2U) << n;
            ++accepted;
        } catch (const ContentError&) {
        }
    }
    EXPECT_EQ(accepted, 2U);  // after the file word, and after the first container
    for (std::size_t i = 0; i < good.size(); ++i) {
        for (const std::uint8_t v : {std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x80}}) {
            Bytes b = good;
            b[i] = v;
            try {
                (void)parseRouteObjectFile(b);
            } catch (const ContentError&) {
                // refusing is fine; crashing or reading out of bounds is not
            }
        }
    }
}

TEST(RouteObjects, GameStopsAtUnknownVersionsInsideTheGround) {
    // readObjectContainer itself follows the game: a version outside 1..5 just ends.
    Bytes b;
    putU32(b, 77);
    std::size_t pos = 0;
    const auto c = readObjectContainer(b, pos);
    EXPECT_EQ(c.version, 77U);
    EXPECT_TRUE(c.items.empty());
    EXPECT_EQ(pos, 4U);
}
