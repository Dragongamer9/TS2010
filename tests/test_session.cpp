#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/SessionFile.h"

#include <gtest/gtest.h>

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
