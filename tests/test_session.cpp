#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/SessionFile.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace trainz::content;
using namespace trainz::test;

namespace {

// A minimal .chump: "ACS$", version 1, reserved 0, body size, then one integer item.
Bytes chumpWithInt(const std::string& key, std::int32_t value) {
    Bytes item;
    const auto keyLen = static_cast<std::uint8_t>(key.size() + 1);
    putU32(item, static_cast<std::uint32_t>(1 + keyLen + 1 + 4));
    item.push_back(keyLen);
    item.insert(item.end(), key.begin(), key.end());
    item.push_back(0);
    item.push_back(1);  // integers
    putU32(item, static_cast<std::uint32_t>(value));
    Bytes c = bytesOf("ACS$");
    putU32(c, 1);
    putU32(c, 0);
    putU32(c, static_cast<std::uint32_t>(item.size()));
    c.insert(c.end(), item.begin(), item.end());
    return c;
}

Bytes behaviourPayload(std::uint32_t version, const std::string& name, const Bytes& chump) {
    Bytes body;
    if (version == 3) {
        putU32(body, 1);
        body.push_back(0);
    }
    putU32(body, static_cast<std::uint32_t>(name.size() + 1));
    body.insert(body.end(), name.begin(), name.end());
    body.push_back(0);
    putU32(body, 0);
    body.insert(body.end(), chump.begin(), chump.end());
    Bytes p;
    putU32(p, version);
    putU32(p, static_cast<std::uint32_t>(body.size()));
    p.insert(p.end(), body.begin(), body.end());
    return p;
}

Bytes container(const char* fileTag, const Bytes& payload) {
    Bytes c;
    putU32(c, 4);  // container version 4: no extra word, a tail
    putU32(c, 0);  // index 0
    c.insert(c.end(), fileTag, fileTag + 4);
    putU32(c, 10212);
    putU32(c, 0xFFFFFFF0u);
    putU32(c, static_cast<std::uint32_t>(payload.size()));
    c.insert(c.end(), payload.begin(), payload.end());
    putU32(c, 0xFFFFFFFFu);  // end of items
    putU32(c, 0);            // empty tail
    return c;
}

void block(Bytes& f, const Bytes& b) {
    putU32(f, static_cast<std::uint32_t>(b.size()));
    f.insert(f.end(), b.begin(), b.end());
}

}  // namespace

TEST(Session, BehavioursBothVersions) {
    for (const std::uint32_t v : {2U, 3U}) {
        ObjectItem it;
        it.tag = "BHVR";
        it.payload = behaviourPayload(v, "Startup Options", chumpWithInt("hour", 10));
        const auto b = decodeBehaviour(it);
        ASSERT_TRUE(b.has_value()) << v;
        EXPECT_EQ(b->version, v);
        EXPECT_EQ(b->name, "Startup Options");
        const auto* hour = b->settings.root.child("hour");
        ASSERT_NE(hour, nullptr);
        EXPECT_EQ(hour->ints.at(0), 10);
    }
}

TEST(Session, ProfileBlocksAndContainers) {
    Bytes f;
    putU32(f, 1);  // version 1: 4 blocks
    ObjectItem it;
    block(f, container("RVHB", behaviourPayload(3, "Driver Setup", chumpWithInt("rate", 1))));
    block(f, container("tCrt", Bytes(40, 7)));
    block(f, Bytes{1, 2, 3});
    block(f, container("blsg", chumpWithInt("helpmode", 1)));
    const auto p = parseSessionProfile(f);
    EXPECT_EQ(p.version, 1U);
    ASSERT_EQ(p.blocks.size(), 4U);
    ASSERT_TRUE(p.blocks[0].container.has_value());
    EXPECT_EQ(p.blocks[0].container->items.at(0).tag, "BHVR");
    EXPECT_EQ(p.blocks[1].container->items.at(0).tag, "trCt");
    EXPECT_FALSE(p.blocks[2].container.has_value());
    EXPECT_EQ(p.blocks[3].container->items.at(0).tag, "gslb");
    EXPECT_TRUE(p.trailer.empty());
    EXPECT_TRUE(p.warnings.empty());
    const auto b = decodeBehaviour(p.blocks[0].container->items[0]);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->name, "Driver Setup");
}

TEST(Session, BadInputIsRefused) {
    Bytes v9;
    putU32(v9, 9);
    EXPECT_THROW(parseSessionProfile(v9), ContentError);
    Bytes overrun;
    putU32(overrun, 0);
    putU32(overrun, 1000);  // block longer than the file
    EXPECT_THROW(parseSessionProfile(overrun), ContentError);
    EXPECT_THROW(parseSessionProfile(Bytes{1, 0}), ContentError);

    ObjectItem wrongSize;
    wrongSize.tag = "BHVR";
    wrongSize.payload = behaviourPayload(3, "X", chumpWithInt("a", 1));
    wrongSize.payload[4] ^= 1;  // size word no longer matches
    EXPECT_FALSE(decodeBehaviour(wrongSize).has_value());
    ObjectItem other;
    other.tag = "trCt";
    other.payload = Bytes(64, 0);
    EXPECT_FALSE(decodeBehaviour(other).has_value());

    // Every truncation and byte change of a valid file is either read or refused.
    Bytes good;
    putU32(good, 0);
    block(good, container("RVHB", behaviourPayload(2, "Wait", chumpWithInt("t", 5))));
    block(good, Bytes(5, 9));
    block(good, container("tCrt", Bytes(12, 1)));
    for (std::size_t i = 0; i < good.size(); ++i) {
        Bytes b = good;
        b[i] ^= 0xFF;
        try {
            const auto p = parseSessionProfile(b);
            for (const auto& bl : p.blocks) {
                if (bl.container) {
                    for (const auto& item : bl.container->items) {
                        (void)decodeBehaviour(item);
                    }
                }
            }
        } catch (const ContentError&) {
        }
        try {
            (void)parseSessionProfile(std::span(good.data(), i));
        } catch (const ContentError&) {
        }
    }
}

namespace {

Bytes vehicleRecord(const std::string& name, std::int16_t bx, std::int16_t by, float x, float y) {
    Bytes r;
    putU32(r, 8);
    putU32(r, 3);
    putU32(r, 0xFF000000u);
    r.push_back(1);  // flag
    putU16(r, static_cast<std::uint16_t>(bx));
    putU16(r, static_cast<std::uint16_t>(by));
    for (const float f : {x, y, 0.3F, 1.5F}) {
        std::uint32_t u;
        std::memcpy(&u, &f, 4);
        putU32(r, u);
    }
    r.resize(41, 0);
    r.push_back(0x80);
    putU32(r, static_cast<std::uint32_t>(name.size() + 1));
    r.insert(r.end(), name.begin(), name.end());
    r.push_back(0);
    r.resize(r.size() + 30, 0);  // more vehicle data, not decoded
    return r;
}

}  // namespace

TEST(Session, TrainsAndVehicles) {
    Bytes p;
    putU32(p, 0xCAFFA1DAu);
    putU32(p, 5);
    putU32(p, 2);  // vehicles
    putU32(p, 0);
    putU32(p, 592);  // track piece
    const float dist = 34.0F;
    std::uint32_t u;
    std::memcpy(&u, &dist, 4);
    putU32(p, u);
    putU32(p, 1);
    for (const auto& [name, content] : {std::pair<std::string, std::int32_t>{"CN SD40-2 1", 100076},
                                        std::pair<std::string, std::int32_t>{"CN 40 FT Box 2", 100077}}) {
        putU32(p, static_cast<std::uint32_t>(content));
        putU32(p, 37187);
        putU32(p, 0);
        p.push_back(0);
        const Bytes r = vehicleRecord(name, 0, -5, 354.4F, 453.9F);
        putU32(p, static_cast<std::uint32_t>(r.size()));
        p.insert(p.end(), r.begin(), r.end());
    }
    const Bytes tail = {0xEB, 0xCE, 0xED, 0xFE, 1, 2, 3};
    p.insert(p.end(), tail.begin(), tail.end());

    ObjectItem it;
    it.tag = "trCt";
    it.payload = p;
    const auto t = decodeTrain(it);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->trackPiece, 592);
    EXPECT_FLOAT_EQ(t->distance, 34.0F);
    ASSERT_EQ(t->vehicles.size(), 2U);
    EXPECT_EQ(t->vehicles[0].name, "CN SD40-2 1");
    EXPECT_EQ(t->vehicles[1].kuid.content, 100077);
    EXPECT_EQ(t->vehicles[1].boardY, -5);
    EXPECT_FLOAT_EQ(t->vehicles[1].x, 354.4F);
    EXPECT_EQ(t->tail, tail);

    // Truncated anywhere inside the vehicles: refused, never read past the end.
    for (std::size_t n = 28; n + tail.size() < p.size(); ++n) {
        ObjectItem cut = it;
        cut.payload.resize(n);
        EXPECT_FALSE(decodeTrain(cut).has_value()) << n;
    }
    ObjectItem old = it;
    old.payload[4] = 4;  // not version 5
    EXPECT_FALSE(decodeTrain(old).has_value());
}

TEST(Session, OldTrainsFoundByPattern) {
    auto vehicle = [](Bytes& p, std::int32_t content, const std::string& name) {
        putU32(p, static_cast<std::uint32_t>(content));
        putU32(p, 0x14011C1Au);
        putU32(p, 0);
        p.push_back(0);
        Bytes r;
        putU32(r, 4);
        putU32(r, 3);
        putU32(r, 0xFF0000FFu);
        putU16(r, static_cast<std::uint16_t>(-3));
        putU16(r, static_cast<std::uint16_t>(-14));
        for (const float f : {378.08F, 277.88F, 0.3F, 0.0F}) {
            std::uint32_t u;
            std::memcpy(&u, &f, 4);
            putU32(r, u);
        }
        r.resize(40, 0);
        putU32(r, static_cast<std::uint32_t>(name.size() + 1));
        r.insert(r.end(), name.begin(), name.end());
        r.push_back(0);
        r.resize(r.size() + 40, 0x11);  // more vehicle data, not decoded
        p.insert(p.end(), r.begin(), r.end());
    };
    Bytes p;
    putU32(p, 2);  // vehicles
    putU32(p, 0);
    putU32(p, 2400);
    putU32(p, 0x41280000u);  // 10.5
    putU32(p, 1);
    vehicle(p, 16022, "Schlackenwagen Ork 5");
    vehicle(p, 16023, "Schlackenwagen Ork 6");
    ObjectItem it;
    it.tag = "trCt";
    it.payload = p;
    const auto t = decodeTrain(it);
    ASSERT_TRUE(t.has_value());
    EXPECT_TRUE(t->oldLayout);
    EXPECT_EQ(t->trackPiece, 2400);
    EXPECT_FLOAT_EQ(t->distance, 10.5F);
    ASSERT_EQ(t->vehicles.size(), 2U);
    EXPECT_EQ(t->vehicles[1].name, "Schlackenwagen Ork 6");
    EXPECT_EQ(t->vehicles[1].kuid.content, 16023);
    EXPECT_EQ(t->vehicles[0].boardY, -14);

    ObjectItem wrongCount = it;
    wrongCount.payload[0] = 3;  // says 3 vehicles, the pattern finds 2: refused
    EXPECT_FALSE(decodeTrain(wrongCount).has_value());
    for (std::size_t n = 0; n < p.size(); ++n) {  // cut anywhere: refused or read, never past the end
        ObjectItem cut = it;
        cut.payload.resize(n);
        (void)decodeTrain(cut);
    }
}
