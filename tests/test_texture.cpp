#include "JaTestBuilder.h"

#include "common/PngWriter.h"
#include "content/ContentError.h"
#include "content/JirfFile.h"
#include "content/TextureFile.h"

#include <gtest/gtest.h>

using namespace trainz::content;
using namespace trainz::test;

namespace {

void putTag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }

Bytes jirfChunk(const char* tag, const Bytes& data) {
    Bytes b;
    putTag(b, tag);
    putU32(b, static_cast<std::uint32_t>(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

Bytes jirf(const char* form, const std::vector<Bytes>& chunks) {
    Bytes body;
    putTag(body, form);
    for (const auto& c : chunks) {
        body.insert(body.end(), c.begin(), c.end());
    }
    Bytes b;
    putTag(b, "JIRF");
    putU32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

// INFO as seen in the game (version 0x107, 20 words).
Bytes info(std::uint32_t mips, PixelFormat f, std::uint32_t w, std::uint32_t h) {
    Bytes b;
    const std::uint32_t words[20] = {0x107, 0, mips, 1, static_cast<std::uint32_t>(f), w, h, 0, 2, 2,
                                     0,     2, 2,    2, 0, 0,                          0, 0, 0, 0};
    for (auto v : words) {
        putU32(b, v);
    }
    return b;
}

// A full mip chain, stored smallest first like the game.
Bytes texture(PixelFormat f, std::uint32_t w, std::uint32_t h, std::uint32_t mips,
              const std::vector<Bytes>& largestFirst) {
    std::vector<Bytes> chunks = {jirfChunk("INFO", info(mips, f, w, h))};
    for (auto it = largestFirst.rbegin(); it != largestFirst.rend(); ++it) {
        chunks.push_back(jirfChunk("MMAP", *it));
    }
    return jirf("MMTF", chunks);
}

MipLevel mip(std::uint32_t w, std::uint32_t h, Bytes data) { return MipLevel{w, h, std::move(data)}; }

}  // namespace

TEST(Jirf, ParsesChunks) {
    const Bytes f = jirf("TEST", {jirfChunk("AAAA", {1, 2, 3}), jirfChunk("BBBB", {})});
    const auto j = parseJirf(f);
    EXPECT_EQ(j.form, "TEST");
    ASSERT_EQ(j.chunks.size(), 2u);
    EXPECT_EQ(j.chunks[0].tag, "AAAA");
    EXPECT_EQ(j.chunks[0].data.size(), 3u);
    EXPECT_EQ(j.chunks[0].offset, 20u);
    EXPECT_TRUE(j.chunks[1].data.empty());
}

TEST(Jirf, DamageThrows) {
    const Bytes good = jirf("TEST", {jirfChunk("AAAA", {1, 2, 3})});
    for (std::size_t len = 0; len < good.size(); ++len) {
        EXPECT_THROW(parseJirf(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len))), ContentError)
            << len;
    }
    Bytes badSize = good;
    setU32(badSize, 16, 1000);
    EXPECT_THROW(parseJirf(badSize), ContentError);
    Bytes badTag = good;
    badTag[12] = 0x01;
    EXPECT_THROW(parseJirf(badTag), ContentError);
}

TEST(Texture, SizesPerFormat) {
    EXPECT_EQ(imageByteSize(PixelFormat::ARGB8888, 64, 64), 64u * 64 * 4);
    EXPECT_EQ(imageByteSize(PixelFormat::RGB0888, 1, 1), 4u);
    EXPECT_EQ(imageByteSize(PixelFormat::DXT1, 64, 64), 16u * 16 * 8);
    EXPECT_EQ(imageByteSize(PixelFormat::DXT1, 1, 1), 8u);  // a whole block, as in the files
    EXPECT_EQ(imageByteSize(PixelFormat::DXT5, 2, 1), 16u);
    EXPECT_EQ(imageByteSize(PixelFormat::DXT3, 8, 4), 32u);
    EXPECT_EQ(imageByteSize(PixelFormat::RGB565, 3, 3), 18u);
    EXPECT_FALSE(imageByteSize(PixelFormat::Null, 1, 1));
    EXPECT_FALSE(imageByteSize(static_cast<PixelFormat>(99), 1, 1));
}

TEST(Texture, ParsesAMipChainSmallestFirst) {
    // 4x2 ARGB8888: levels 4x2, 2x1, 1x1
    const Bytes l0(4 * 2 * 4, 0x11), l1(2 * 1 * 4, 0x22), l2(4, 0x33);
    const auto t = parseTexture(texture(PixelFormat::ARGB8888, 4, 2, 3, {l0, l1, l2}));
    EXPECT_EQ(t.info.version, 0x107u);
    EXPECT_EQ(t.info.format, PixelFormat::ARGB8888);
    ASSERT_EQ(t.mips.size(), 3u);
    EXPECT_EQ(t.mips[0].width, 4u);
    EXPECT_EQ(t.mips[0].height, 2u);
    EXPECT_EQ(t.mips[0].data, l0);
    EXPECT_EQ(t.mips[1].width, 2u);
    EXPECT_EQ(t.mips[1].height, 1u);
    EXPECT_EQ(t.mips[2].data, l2);
    EXPECT_EQ(t.info.words.size(), 20u);
}

TEST(Texture, RejectsInconsistentFiles) {
    const Bytes l0(16, 0), l1(4, 0);
    EXPECT_THROW(parseTexture(texture(PixelFormat::ARGB8888, 2, 2, 3, {l0, l1})), ContentError);  // count
    EXPECT_THROW(parseTexture(texture(PixelFormat::ARGB8888, 2, 2, 2, {l0, Bytes(5, 0)})), ContentError);
    EXPECT_THROW(parseTexture(texture(static_cast<PixelFormat>(42), 2, 2, 2, {l0, l1})), ContentError);
    EXPECT_THROW(parseTexture(texture(PixelFormat::ARGB8888, 0, 2, 2, {l0, l1})), ContentError);
    EXPECT_THROW(parseTexture(texture(PixelFormat::ARGB8888, 9000, 2, 2, {l0, l1})), ContentError);
    EXPECT_THROW(parseTexture(jirf("IDXM", {})), ContentError);
    EXPECT_THROW(parseTexture(jirf("MMTF", {jirfChunk("MMAP", l1)})), ContentError);  // no INFO
}

TEST(Texture, EveryTruncationAndByteChangeIsSafe) {
    const Bytes good = texture(PixelFormat::DXT1, 4, 4, 3, {Bytes(8, 0xAB), Bytes(8, 0xCD), Bytes(8, 0xEF)});
    auto tryAll = [](const Bytes& b) {
        try {
            const auto t = parseTexture(b);
            for (const auto& m : t.mips) {
                (void)decodeToRgba8(m, t.info.format);
            }
        } catch (const ContentError&) {
        }
    };
    for (std::size_t len = 0; len <= good.size(); ++len) {
        tryAll(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len)));
    }
    for (std::size_t i = 0; i < good.size(); ++i) {
        for (std::uint8_t v : {std::uint8_t{0}, std::uint8_t{0xFF}, std::uint8_t{0x0C}}) {
            Bytes b = good;
            b[i] = v;
            tryAll(b);
        }
    }
}

TEST(Decode, Argb8888IsBgraInMemory) {
    // little-endian ARGB: bytes B, G, R, A
    const auto out = decodeToRgba8(mip(1, 1, {0x10, 0x20, 0x30, 0x40}), PixelFormat::ARGB8888);
    EXPECT_EQ(out, (Bytes{0x30, 0x20, 0x10, 0x40}));
    const auto opaque = decodeToRgba8(mip(1, 1, {0x10, 0x20, 0x30, 0x00}), PixelFormat::RGB0888);
    EXPECT_EQ(opaque, (Bytes{0x30, 0x20, 0x10, 0xFF}));
}

TEST(Decode, Dxt1Block) {
    // c0 = pure red (0xF800), c1 = pure blue (0x001F), c0 > c1: 4 colours
    // indices: pixel 0 -> 0 (red), 1 -> 1 (blue), 2 -> 2 (2/3 red), 3 -> 3 (1/3 red); rest 0
    const Bytes blk = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x00, 0x00, 0x00};
    const auto out = decodeToRgba8(mip(4, 4, blk), PixelFormat::DXT1);
    ASSERT_EQ(out.size(), 64u);
    EXPECT_EQ(Bytes(out.begin(), out.begin() + 4), (Bytes{255, 0, 0, 255}));
    EXPECT_EQ(Bytes(out.begin() + 4, out.begin() + 8), (Bytes{0, 0, 255, 255}));
    EXPECT_EQ(Bytes(out.begin() + 8, out.begin() + 12), (Bytes{170, 0, 85, 255}));
    EXPECT_EQ(Bytes(out.begin() + 12, out.begin() + 16), (Bytes{85, 0, 170, 255}));
    EXPECT_EQ(Bytes(out.begin() + 16, out.begin() + 20), (Bytes{255, 0, 0, 255}));
}

TEST(Decode, Dxt1TransparentWhenC0NotGreater) {
    // c0 = c1 = blue: 3-colour mode, index 3 = transparent black
    const Bytes blk = {0x1F, 0x00, 0x1F, 0x00, 0x03, 0x00, 0x00, 0x00};
    const auto out = decodeToRgba8(mip(4, 4, blk), PixelFormat::DXT1);
    EXPECT_EQ(Bytes(out.begin(), out.begin() + 4), (Bytes{0, 0, 0, 0}));
    EXPECT_EQ(Bytes(out.begin() + 4, out.begin() + 8), (Bytes{0, 0, 255, 255}));
}

TEST(Decode, Dxt3AndDxt5Alpha) {
    const Bytes colour = {0x00, 0xF8, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00};  // all red
    Bytes dxt3 = {0x0F, 0xF0, 0, 0, 0, 0, 0, 0};                             // alpha nibbles: 15, 0, 0, 15, 0...
    dxt3.insert(dxt3.end(), colour.begin(), colour.end());
    const auto a3 = decodeToRgba8(mip(4, 4, dxt3), PixelFormat::DXT3);
    EXPECT_EQ(a3[3], 255);
    EXPECT_EQ(a3[7], 0);
    EXPECT_EQ(a3[15], 255);

    // DXT5: a0 = 200, a1 = 100 (8-step mode); pixel 0 index 0 (200), pixel 1 index 1 (100), pixel 2 index 2
    Bytes dxt5 = {200, 100, 0x88, 0x00, 0, 0, 0, 0};  // bits: p0=0, p1=1 (<<3), p2=2 (<<6) = 0x88
    dxt5.insert(dxt5.end(), colour.begin(), colour.end());
    const auto a5 = decodeToRgba8(mip(4, 4, dxt5), PixelFormat::DXT5);
    EXPECT_EQ(a5[3], 200);
    EXPECT_EQ(a5[7], 100);
    EXPECT_EQ(a5[11], (6 * 200 + 1 * 100) / 7);
}

TEST(Decode, Dxt5SixStepModeHasZeroAndFull) {
    const Bytes colour = {0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0};
    // a0 = 10 <= a1 = 20; pixel 0 -> index 6 (0), pixel 1 -> index 7 (255)
    Bytes dxt5 = {10, 20, 0x3E, 0, 0, 0, 0, 0};  // p0 = 6 (110), p1 = 7 (111): bits 0x3E
    dxt5.insert(dxt5.end(), colour.begin(), colour.end());
    const auto a5 = decodeToRgba8(mip(4, 4, dxt5), PixelFormat::DXT5);
    EXPECT_EQ(a5[3], 0);
    EXPECT_EQ(a5[7], 255);
}

TEST(Decode, OversizedDimensionsAreRefused) {
    // 0x80000000 x 0x80000000 x 4 would wrap to 0 on 64-bit without the size limit.
    EXPECT_FALSE(imageByteSize(PixelFormat::ARGB8888, 0x80000000u, 0x80000000u));
    EXPECT_FALSE(imageByteSize(PixelFormat::DXT1, 0xFFFFFFFFu, 4));  // width + 3 would wrap
    EXPECT_FALSE(imageByteSize(PixelFormat::DXT5, 8193, 1));
    EXPECT_FALSE(imageByteSize(PixelFormat::RGB565, 0, 1));
    EXPECT_TRUE(imageByteSize(PixelFormat::ARGB8888, 8192, 8192));
    for (PixelFormat f : {PixelFormat::ARGB8888, PixelFormat::RGB0888, PixelFormat::DXT1, PixelFormat::DXT5,
                          PixelFormat::RGB565}) {
        EXPECT_THROW(decodeToRgba8(mip(0x80000000u, 0x80000000u, {}), f), ContentError);
        EXPECT_THROW(decodeToRgba8(mip(0xFFFFFFFFu, 1, {}), f), ContentError);
        EXPECT_THROW(decodeToRgba8(mip(0, 4, {}), f), ContentError);
        EXPECT_THROW(decodeToRgba8(mip(4, 4, {}), f), ContentError);  // data missing
    }
}

TEST(Decode, NonMultipleOfFourSizes) {
    const auto out = decodeToRgba8(mip(2, 1, Bytes(8, 0)), PixelFormat::DXT1);
    EXPECT_EQ(out.size(), 2u * 1 * 4);
}

TEST(Png, WritesAValidFile) {
    const Bytes rgba = {255, 0, 0, 255, 0, 255, 0, 128};
    const auto png = trainz::common::encodePngRgba(2, 1, rgba);
    ASSERT_GT(png.size(), 33u);
    EXPECT_EQ(Bytes(png.begin(), png.begin() + 8), (Bytes{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'}));
    // IHDR CRC covers type + data
    const std::uint32_t stored = (std::uint32_t{png[29]} << 24) | (std::uint32_t{png[30]} << 16) |
                                 (std::uint32_t{png[31]} << 8) | png[32];
    EXPECT_EQ(trainz::common::crc32(std::span<const std::uint8_t>(png.data() + 12, 17)), stored);
    // The stored deflate block holds the raw scanline: filter 0, then the pixels.
    const std::size_t idat = 33 + 8 + 2 + 5;  // IDAT length+type, zlib header, block header
    EXPECT_EQ(png[idat], 0);
    EXPECT_EQ(Bytes(png.begin() + static_cast<std::ptrdiff_t>(idat + 1),
                    png.begin() + static_cast<std::ptrdiff_t>(idat + 9)),
              rgba);
    EXPECT_THROW(trainz::common::encodePngRgba(3, 1, rgba), std::invalid_argument);
}

TEST(Png, CrcKnownValue) {
    const Bytes s = bytesOf("123456789");
    EXPECT_EQ(trainz::common::crc32(s), 0xCBF43926u);
}
