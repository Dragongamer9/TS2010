// Damaged archives must never crash the reader or make it read out of bounds:
// every failure has to come out as a ContentError. Run under AddressSanitizer
// and UBSan in CI, this checks every truncation length and a set of byte
// changes at every position of a small archive.
#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/JaArchive.h"

#include <gtest/gtest.h>

using namespace trainz::content;
using namespace trainz::test;

namespace {

Bytes smallArchive() {
    JaTestBuilder b;
    b.addPlain("a/config.txt", bytesOf("kuid <kuid:1:2>"));
    b.addCfilRaw("a/raw.bin", patternBytes(40, 1));
    b.addCfilBpe("a/bpe.bin", patternBytes(60, 2));
    // A packed block with real pairs: "ababc".
    Bytes pairs = cfilHeader(5);
    const Bytes payload = {0xFF, 0xFE, 0x00, 'a', 'b', 0x00, 0x03, 0xFF, 0xFF, 'c'};
    putU16(pairs, static_cast<std::uint16_t>(payload.size()));
    pairs.insert(pairs.end(), payload.begin(), payload.end());
    b.addEntry({"a/pairs.bin", pairs, 5, 0x80, 0});
    return b.build();
}

// Opens the file and reads every entry both ways. Only ContentError may escape.
void exercise(const std::filesystem::path& p) {
    try {
        const auto a = JaArchive::open(p);
        Bytes buf(13);
        for (const auto& e : a.entries()) {
            try {
                (void)a.read(e);
            } catch (const ContentError&) {
            }
            try {
                auto s = a.openStream(e);
                while (s.read(buf) != 0) {
                }
            } catch (const ContentError&) {
            }
            (void)a.find(e.name);
        }
    } catch (const ContentError&) {
    }
}

}  // namespace

TEST(Corruption, SampleArchiveIsValid) {
    TempDir dir;
    const auto a = JaArchive::open(dir.write("ok.ja", smallArchive()));
    ASSERT_EQ(a.entries().size(), 4u);
    EXPECT_EQ(a.read(*a.find("a/pairs.bin")), bytesOf("ababc"));
}

TEST(Corruption, EveryTruncation) {
    TempDir dir;
    const Bytes good = smallArchive();
    for (std::size_t len = 0; len < good.size(); ++len) {
        const auto p = dir.write("t.ja", Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len)));
        ASSERT_NO_FATAL_FAILURE(exercise(p)) << "length " << len;
    }
}

TEST(Corruption, EveryByteChanged) {
    TempDir dir;
    const Bytes good = smallArchive();
    for (std::size_t pos = 0; pos < good.size(); ++pos) {
        for (std::uint8_t change : {std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x80}}) {
            Bytes b = good;
            if (b[pos] == change) {
                continue;
            }
            b[pos] = change;
            const auto p = dir.write("c.ja", b);
            ASSERT_NO_FATAL_FAILURE(exercise(p)) << "byte " << pos << " set to " << int(change);
        }
    }
}
