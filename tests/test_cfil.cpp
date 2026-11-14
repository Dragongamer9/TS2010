#include "JaTestBuilder.h"

#include "content/CfilCodec.h"
#include "content/ContentError.h"

#include <gtest/gtest.h>

using namespace trainz::content;
using namespace trainz::test;

namespace {

Bytes expand(const Bytes& payload, std::size_t max = 0x4000) {
    Bytes out;
    expandBpeBlock(payload, out, max);
    return out;
}

std::string text(const Bytes& b) { return std::string(b.begin(), b.end()); }

}  // namespace

TEST(Bpe, AllLiteralTable) {
    const Bytes data = bytesOf("kuid <kuid:1:2>");
    EXPECT_EQ(expand(bpeLiteralSubBlock(data)), data);
}

TEST(Bpe, OnePair) {
    // skip 128, skip 127 -> code 255; one entry: 255 = ('a','b'); data FF FF 'c'
    const Bytes payload = {0xFF, 0xFE, 0x00, 'a', 'b', 0x00, 0x03, 0xFF, 0xFF, 'c'};
    EXPECT_EQ(text(expand(payload)), "ababc");
}

TEST(Bpe, NestedPairExpandsLeftFirst) {
    // 254 = (255,'c'), 255 = ('a','b'); data 254 -> "abc"
    const Bytes payload = {0xFF, 0xFD, 0x01, 0xFF, 'c', 'a', 'b', 0x00, 0x01, 0xFE};
    EXPECT_EQ(text(expand(payload)), "abc");
}

TEST(Bpe, SkipDoesNotAlsoReadAPair) {
    // In Gage's original a skip is followed by one pair. In the game's version
    // (0x1000d1a6) it is not: after FF FE we are at code 255 and read a new count.
    // If the decoder wrongly read a pair after each skip, this table would be misparsed.
    const Bytes payload = {0xFF, 0xFE, 0x00, 'x', 'y', 0x00, 0x01, 0xFF};
    EXPECT_EQ(text(expand(payload)), "xy");
}

TEST(Bpe, SeveralSubBlocksInOnePayload) {
    Bytes payload = bpeLiteralSubBlock(bytesOf("hello "));
    const Bytes second = {0xFF, 0xFE, 0x00, 'o', 'k', 0x00, 0x02, 0xFF, '!'};
    payload.insert(payload.end(), second.begin(), second.end());
    EXPECT_EQ(text(expand(payload)), "hello ok!");
}

TEST(Bpe, RightByteOmittedWhenLeftEqualsCode) {
    // Explicitly listing code 'A' as itself (left == code) has no right byte.
    // skip 65 (0xC0 -> 65) to code 'A'; count 0: left['A'] = 'A'; then skip the rest.
    const Bytes payload = {0xC0, 0x00, 'A', 0xFF, 0xBE, 0x00, 0x01, 'A'};
    EXPECT_EQ(text(expand(payload)), "A");
}

TEST(Bpe, TruncatedInputThrows) {
    const Bytes payload = {0xFF, 0xFE, 0x00, 'a'};
    EXPECT_THROW(expand(payload), ContentError);
    const Bytes noData = {0xFF, 0xFF, 0x00, 0x05, 'a', 'b'};
    EXPECT_THROW(expand(noData), ContentError);
}

TEST(Bpe, PairTablePast256Throws) {
    const Bytes payload = {0xFF, 0xFE, 0x05, 'a', 'b', 'c', 'd'};
    EXPECT_THROW(expand(payload), ContentError);
}

TEST(Bpe, CyclicTableThrowsInsteadOfLooping) {
    // 254 = (255, 0), 255 = (254, 0): expands forever in the original.
    const Bytes payload = {0xFF, 0xFD, 0x01, 0xFF, 0x00, 0xFE, 0x00, 0x00, 0x01, 0xFE};
    EXPECT_THROW(expand(payload), ContentError);
}

TEST(Bpe, OutputLimitIsEnforced) {
    EXPECT_THROW(expand(bpeLiteralSubBlock(Bytes(100, 'z')), 99), ContentError);
}

TEST(Cfil, RawBlocks) {
    const Bytes data = patternBytes(40000, 1);
    EXPECT_EQ(decodeCfil(cfilRaw(data), 0x102, 0), data);
}

TEST(Cfil, PackedBlocks) {
    const Bytes data = patternBytes(0x4000 * 2 + 17, 2);
    EXPECT_EQ(decodeCfil(cfilBpe(data), 0x102, 0), data);
}

TEST(Cfil, HeaderSizeWins) {
    const Bytes data = bytesOf("abcdef");
    // Entry size is ignored when the header has a size (version >= 0x101).
    EXPECT_EQ(decodeCfil(cfilRaw(data), 0x102, 999), data);
}

TEST(Cfil, Version101ArchiveHasMagicOnly) {
    const Bytes data = bytesOf("old archive format");
    const Bytes stored = cfilRaw(data, 0x101);
    ASSERT_EQ(stored.size(), 4u + 2u + data.size());
    EXPECT_EQ(decodeCfil(stored, 0x101, static_cast<std::uint32_t>(data.size())), data);
}

TEST(Cfil, EmptyFile) {
    EXPECT_TRUE(decodeCfil(cfilRaw(Bytes{}), 0x102, 0).empty());
}

TEST(Cfil, DamagedDataThrows) {
    const Bytes data = patternBytes(1000, 3);
    Bytes stored = cfilRaw(data);
    EXPECT_THROW(decodeCfil(Bytes(stored.begin(), stored.end() - 1), 0x102, 0), ContentError);
    EXPECT_THROW(decodeCfil(Bytes(stored.begin(), stored.begin() + 8), 0x102, 0), ContentError);
    Bytes zeroBlock = cfilHeader(10);
    putU16(zeroBlock, 0x8000);
    EXPECT_THROW(decodeCfil(zeroBlock, 0x102, 0), ContentError);
    Bytes tooLong = cfilHeader(4);
    putU16(tooLong, 0x8000 | 5);
    tooLong.insert(tooLong.end(), {1, 2, 3, 4, 5});
    EXPECT_THROW(decodeCfil(tooLong, 0x102, 0), ContentError);
    EXPECT_THROW(decodeCfil(bytesOf("CFIX0000"), 0x102, 0), ContentError);
}
