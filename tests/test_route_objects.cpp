#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/ObjectContainer.h"
#include "content/PlacedObject.h"
#include "content/TrackLayout.h"

#include <gtest/gtest.h>

#include <cstring>
#include <limits>

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

namespace {

void putF32(Bytes& b, float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    putU32(b, u);
}

Bytes vertexPayload(std::int16_t bx, std::int16_t by, float x, float y, float z, std::vector<std::int32_t> links = {}) {
    Bytes p;
    putU32(p, 3);
    putU16(p, static_cast<std::uint16_t>(bx));
    putU16(p, static_cast<std::uint16_t>(by));
    putF32(p, x);
    putF32(p, y);
    putF32(p, z);
    links.resize(4, -1);  // empty slots are -1
    for (const auto l : links) {
        putU32(p, static_cast<std::uint32_t>(l));
        p.push_back(0);  // flag
    }
    p.push_back(0);  // last byte
    return p;
}

Bytes stretchPayload(std::int32_t from, std::int32_t to) {
    Bytes p;
    putU32(p, 6);
    putU32(p, static_cast<std::uint32_t>(from));
    putU32(p, static_cast<std::uint32_t>(to));
    putU32(p, 1116);  // track KUID: content, then packed user
    putU32(p, 523);
    p.resize(30, 0);
    return p;
}

}  // namespace

TEST(TrackLayout, DecodesVerticesAndStretches) {
    Bytes b;
    putU32(b, 2);
    putU32(b, 5);
    putU32(b, 2);
    item(b, 0, "tSkt", 1116, 523, stretchPayload(10, 11));
    item(b, 1, "tSkt", 1116, 523, stretchPayload(11, 99));  // vertex 99 does not exist
    endContainer(b, 5, 2);
    putU32(b, 5);
    putU32(b, 2);
    item(b, 10, "xVkt", -1, 0x1FFFFFFu, vertexPayload(-1, 3, 600.0F, 237.5F, 30.0F, {0}));
    item(b, 11, "xVkt", -1, 0x1FFFFFFu, vertexPayload(0, 3, 10.0F, 240.0F, 30.5F, {0, 1}));
    endContainer(b, 5, 2);

    const auto t = readTrackLayout(parseRouteObjectFile(b));
    ASSERT_EQ(t.vertices.size(), 2U);
    const auto& v = t.vertices.at(10);
    EXPECT_EQ(v.boardX, -1);
    EXPECT_EQ(v.boardY, 3);
    EXPECT_FLOAT_EQ(v.z, 30.0F);
    EXPECT_DOUBLE_EQ(v.worldX(), -720.0 + 600.0);
    EXPECT_DOUBLE_EQ(v.worldY(), 3 * 720.0 + 237.5);
    ASSERT_EQ(t.stretches.size(), 2U);
    EXPECT_EQ(t.stretches[0].from, 10);
    EXPECT_EQ(t.stretches[0].to, 11);
    EXPECT_EQ(t.stretches[0].track.user, 523);
    EXPECT_EQ(t.stretches[0].track.content, 1116);
    EXPECT_EQ(t.stretches[0].rest.size(), 10U);
    ASSERT_EQ(v.links.size(), 1U);
    EXPECT_EQ(v.links[0].item, 0);
    EXPECT_EQ(t.vertices.at(11).links.size(), 2U);
    EXPECT_TRUE(v.rest.empty());
    ASSERT_EQ(t.warnings.size(), 1U);  // the stretch to the missing vertex 99
    EXPECT_NE(t.warnings[0].find("missing"), std::string::npos);
}

TEST(TrackLayout, LinksMustPointAtPiecesEndingThere) {
    Bytes b;
    putU32(b, 2);
    putU32(b, 5);
    putU32(b, 2);
    item(b, 0, "tSkt", 1116, 523, stretchPayload(10, 11));
    Bytes wrapped;
    putU32(wrapped, 2);  // tkBr: its own word, then a whole stretch
    const Bytes inner = stretchPayload(11, 12);
    wrapped.insert(wrapped.end(), inner.begin(), inner.end());
    wrapped.resize(94, 0xFF);
    item(b, 1, "rBkt", 1116, 523, wrapped);
    endContainer(b, 5, 2);
    putU32(b, 5);
    putU32(b, 2);
    item(b, 10, "xVkt", -1, 0x1FFFFFFu, vertexPayload(0, 0, 1.0F, 1.0F, 0.0F, {0}));
    item(b, 11, "xVkt", -1, 0x1FFFFFFu, vertexPayload(0, 0, 2.0F, 1.0F, 0.0F, {0, 1}));
    item(b, 12, "xVkt", -1, 0x1FFFFFFu, vertexPayload(0, 0, 3.0F, 1.0F, 0.0F, {1, 0, 7}));  // 0 doesn't end here; 7 missing
    endContainer(b, 5, 2);
    const auto t = readTrackLayout(parseRouteObjectFile(b));
    ASSERT_EQ(t.stretches.size(), 2U);
    EXPECT_EQ(t.stretches[1].kind, "tkBr");
    EXPECT_EQ(t.stretches[1].wrapperVersion, 2U);
    EXPECT_EQ(t.stretches[1].from, 11);
    EXPECT_EQ(t.stretches[1].to, 12);
    EXPECT_EQ(t.vertices.at(12).links.size(), 3U);
    ASSERT_EQ(t.warnings.size(), 2U);
    EXPECT_NE(t.warnings[0].find("does not end there"), std::string::npos);
    EXPECT_NE(t.warnings[1].find("not in the file"), std::string::npos);
}

TEST(TrackLayout, ShortOrOddItemsWarn) {
    ObjectItem shortVertex;
    shortVertex.tag = "tkVx";
    shortVertex.payload = Bytes(10, 0);
    EXPECT_FALSE(decodeTrackVertex(shortVertex).has_value());
    ObjectItem other;
    other.tag = "mOBJ";
    other.payload = Bytes(40, 0);
    EXPECT_FALSE(decodeTrackStretch(other).has_value());

    RouteObjectFile f;
    f.containers.emplace_back();
    ObjectItem nanVertex;
    nanVertex.tag = "tkVx";
    nanVertex.payload = vertexPayload(0, 0, std::numeric_limits<float>::quiet_NaN(), 1.0F, 1.0F);  // links all empty
    f.containers[0].items = {shortVertex, nanVertex, other};
    const auto t = readTrackLayout(f);
    EXPECT_TRUE(t.vertices.empty());
    EXPECT_EQ(t.otherItems, 1U);
    EXPECT_EQ(t.warnings.size(), 2U);
}

namespace {

Bytes baseBlock(std::uint8_t flag, std::int16_t bx, std::int16_t by, float x, float y, float z, float unknown) {
    Bytes p;
    p.push_back(flag);
    putU16(p, static_cast<std::uint16_t>(bx));
    putU16(p, static_cast<std::uint16_t>(by));
    putF32(p, x);
    putF32(p, y);
    putF32(p, z);
    putF32(p, unknown);
    return p;
}

ObjectItem objectItem(const char* tag, Bytes payload) {
    ObjectItem it;
    it.index = 7;
    it.tag = tag;
    it.kuid.user = 523;
    it.kuid.content = 1365;
    it.payload = std::move(payload);
    return it;
}

}  // namespace

TEST(PlacedObject, StubAndObjectBaseBlocks) {
    Bytes stub = baseBlock(1, -1, 3, 375.07F, 184.27F, 29.99F, 45.47F);
    stub.resize(41, 0);  // the rest of a disc mapobjectstub
    const auto s = decodePlacedObject(objectItem("mobs", stub));
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->flag, 1);
    EXPECT_EQ(s->boardX, -1);
    EXPECT_EQ(s->boardY, 3);
    EXPECT_FLOAT_EQ(s->z, 29.99F);
    EXPECT_DOUBLE_EQ(s->worldY(), 3 * 720.0 + static_cast<double>(184.27F));
    EXPECT_EQ(s->rest.size(), 41U - 21U);
    EXPECT_EQ(s->kuid.content, 1365);

    Bytes obj;
    putU32(obj, 3);
    putU32(obj, 0xFF000000u);
    const Bytes base = baseBlock(0, 2, -4, 10.0F, 700.0F, 120.5F, 0.0F);
    obj.insert(obj.end(), base.begin(), base.end());
    const auto o = decodePlacedObject(objectItem("sgOb", obj));
    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o->flag, 0);
    EXPECT_EQ(o->boardY, -4);
    EXPECT_TRUE(o->rest.empty());
}

TEST(PlacedObject, RefusesWhatDoesNotFit) {
    EXPECT_FALSE(decodePlacedObject(objectItem("mobs", baseBlock(2, 0, 0, 1, 1, 1, 0))).has_value());     // flag
    EXPECT_FALSE(decodePlacedObject(objectItem("mobs", baseBlock(1, 0, 0, 720, 1, 1, 0))).has_value());   // x
    EXPECT_FALSE(decodePlacedObject(objectItem("mobs", baseBlock(1, 0, 0, 1, -1, 1, 0))).has_value());    // y
    EXPECT_FALSE(decodePlacedObject(objectItem("mobs", baseBlock(1, 0, 0, 1, 1, std::numeric_limits<float>::infinity(), 0)))
                     .has_value());                                                                       // z
    EXPECT_FALSE(decodePlacedObject(objectItem("mobs", Bytes(20, 0))).has_value());                       // short
    EXPECT_FALSE(decodePlacedObject(objectItem("tkSt", Bytes(41, 0))).has_value());                       // not placed
    EXPECT_FALSE(placedObjectHeaderSize("Rule").has_value());
    EXPECT_EQ(placedObjectHeaderSize("mOBJ"), 8U);
}
