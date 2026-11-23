// Loose image decoders (.tga, .bmp, .jpg): every sub-type found on the disc,
// built here in code, plus damaged input that must throw and never crash.
#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/ImageFile.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#if TRAINZ_HAVE_JPEG
#include <cstdio>

#include <jpeglib.h>
#endif

using namespace trainz::content;
using namespace trainz::test;

// The builders below take designated initializers and rely on the members'
// defaults for the fields left out; GCC's -Wextra would flag each of those.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

namespace {

using Px = std::array<std::uint8_t, 4>;

Px at(const RgbaImage& img, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
    return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2], img.rgba[i + 3]};
}

// Every shorter prefix of a good file must throw ContentError (and not crash).
template <typename F>
void everyPrefixThrows(const Bytes& good, F decode, std::size_t keep = 0) {
    for (std::size_t len = 0; len + keep < good.size(); ++len) {
        const Bytes cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len));
        EXPECT_THROW(decode(cut), ContentError) << "prefix " << len << " of " << good.size();
    }
}

// Flips bytes at pseudo-random places; the decoder may succeed or throw ContentError, nothing else.
template <typename F>
void mutationsNeverCrash(const Bytes& good, F decode, unsigned rounds = 3000) {
    std::uint32_t seed = 12345;
    auto next = [&seed] {
        seed = seed * 1103515245u + 12345u;
        return seed >> 8;
    };
    for (unsigned r = 0; r < rounds; ++r) {
        Bytes b = good;
        const unsigned flips = 1 + next() % 4;
        for (unsigned k = 0; k < flips; ++k) {
            b[next() % b.size()] = static_cast<std::uint8_t>(next());
        }
        try {
            const RgbaImage img = decode(b);
            ASSERT_EQ(img.rgba.size(), static_cast<std::size_t>(img.width) * img.height * 4);
        } catch (const ContentError&) {
        }
    }
}

// ---------------------------------------------------------------- TGA builders

struct TgaHead {
    std::uint8_t type = 2;
    std::uint16_t w = 1, h = 1;
    std::uint8_t bpp = 24;
    std::uint8_t desc = 0;
    std::uint8_t mapType = 0;
    std::uint16_t mapFirst = 0, mapLen = 0;
    std::uint8_t mapBits = 0;
    std::string id;
};

Bytes tga(const TgaHead& t, const Bytes& map, const Bytes& pixels, bool footer = true) {
    Bytes b;
    b.push_back(static_cast<std::uint8_t>(t.id.size()));
    b.push_back(t.mapType);
    b.push_back(t.type);
    putU16(b, t.mapFirst);
    putU16(b, t.mapLen);
    b.push_back(t.mapBits);
    putU16(b, 0);  // x origin
    putU16(b, 0);  // y origin
    putU16(b, t.w);
    putU16(b, t.h);
    b.push_back(t.bpp);
    b.push_back(t.desc);
    b.insert(b.end(), t.id.begin(), t.id.end());
    b.insert(b.end(), map.begin(), map.end());
    b.insert(b.end(), pixels.begin(), pixels.end());
    if (footer) {  // TGA 2.0 footer: 8 zero bytes + signature (most disc files have it)
        for (int i = 0; i < 8; ++i) {
            b.push_back(0);
        }
        const std::string sig = "TRUEVISION-XFILE.";
        b.insert(b.end(), sig.begin(), sig.end());
        b.push_back(0);
    }
    return b;
}

// 2x2 true-colour, rows stored bottom first: bottom row red, green; top row blue, white.
Bytes tga24BottomLeft() {
    return tga({.type = 2, .w = 2, .h = 2, .bpp = 24},
               {}, {0, 0, 255, 0, 255, 0, /* top: */ 255, 0, 0, 255, 255, 255});
}

}  // namespace

TEST(ImageKind, FromName) {
    EXPECT_EQ(imageKindFromName("a/b/C.TGA"), ImageKind::Tga);
    EXPECT_EQ(imageKindFromName("x.bmp"), ImageKind::Bmp);
    EXPECT_EQ(imageKindFromName("x.jpg"), ImageKind::Jpeg);
    EXPECT_EQ(imageKindFromName("x.JPEG"), ImageKind::Jpeg);
    EXPECT_EQ(imageKindFromName("x.texture"), ImageKind::Unknown);
    EXPECT_EQ(imageKindFromName("noext"), ImageKind::Unknown);
    EXPECT_THROW(decodeImage(ImageKind::Unknown, Bytes{1, 2, 3}), ContentError);
}

TEST(Tga, Type2Rgb24BottomLeftOrigin) {
    const auto img = decodeTga(tga24BottomLeft());
    ASSERT_EQ(img.width, 2u);
    ASSERT_EQ(img.height, 2u);
    EXPECT_EQ(at(img, 0, 0), (Px{0, 0, 255, 255}));      // top-left: blue
    EXPECT_EQ(at(img, 1, 0), (Px{255, 255, 255, 255}));  // top-right: white
    EXPECT_EQ(at(img, 0, 1), (Px{255, 0, 0, 255}));      // bottom-left: red
    EXPECT_EQ(at(img, 1, 1), (Px{0, 255, 0, 255}));
    EXPECT_FALSE(img.alphaFromFile);
    EXPECT_NE(img.description.find("type 2"), std::string::npos);
}

TEST(Tga, Type2Rgba32TopLeftOriginKeepsAlpha) {
    const auto img = decodeTga(tga({.type = 2, .w = 2, .h = 1, .bpp = 32, .desc = 0x28}, {},
                                   {1, 2, 3, 4, 5, 6, 7, 8}));
    EXPECT_EQ(at(img, 0, 0), (Px{3, 2, 1, 4}));
    EXPECT_EQ(at(img, 1, 0), (Px{7, 6, 5, 8}));
    EXPECT_TRUE(img.alphaFromFile);
}

TEST(Tga, Type2Rgba32WithZeroAlphaBitsStillUsesTheFourthByte) {
    // 452 disc files say 0 alpha bits, but 304 of them have real (mixed) alpha.
    const auto img = decodeTga(tga({.type = 2, .w = 1, .h = 1, .bpp = 32, .desc = 0}, {}, {10, 20, 30, 40}));
    EXPECT_EQ(at(img, 0, 0), (Px{30, 20, 10, 40}));
}

TEST(Tga, RightToLeft) {
    const auto img = decodeTga(tga({.type = 2, .w = 2, .h = 1, .bpp = 24, .desc = 0x30}, {},
                                   {0, 0, 255, 255, 0, 0}));
    EXPECT_EQ(at(img, 0, 0), (Px{0, 0, 255, 255}));  // stored second = leftmost
    EXPECT_EQ(at(img, 1, 0), (Px{255, 0, 0, 255}));
}

TEST(Tga, Type2Rgb16WithAndWithoutAlphaBit) {
    // A1R5G5B5: 0x7C00 = red with alpha bit clear, 0x83E0 = green with alpha bit set.
    const Bytes px = {0x00, 0x7C, 0xE0, 0x83};
    const auto noAlpha = decodeTga(tga({.type = 2, .w = 2, .h = 1, .bpp = 16, .desc = 0x20}, {}, px));
    EXPECT_EQ(at(noAlpha, 0, 0), (Px{255, 0, 0, 255}));
    EXPECT_EQ(at(noAlpha, 1, 0), (Px{0, 255, 0, 255}));
    EXPECT_FALSE(noAlpha.alphaFromFile);
    const auto withAlpha = decodeTga(tga({.type = 2, .w = 2, .h = 1, .bpp = 16, .desc = 0x21}, {}, px));
    EXPECT_EQ(at(withAlpha, 0, 0), (Px{255, 0, 0, 0}));
    EXPECT_EQ(at(withAlpha, 1, 0), (Px{0, 255, 0, 255}));
    EXPECT_TRUE(withAlpha.alphaFromFile);
    // 15 bpp is the same layout.
    EXPECT_NO_THROW(decodeTga(tga({.type = 2, .w = 2, .h = 1, .bpp = 15}, {}, px)));
}

TEST(Tga, Type1ColourMappedWithImageId) {
    const Bytes map = {0, 0, 255, /* 1: */ 0, 255, 0, /* 2: */ 255, 0, 0};  // 24-bit BGR entries
    const auto img = decodeTga(tga({.type = 1, .w = 3, .h = 1, .bpp = 8, .desc = 0x20, .mapType = 1,
                                    .mapFirst = 0, .mapLen = 3, .mapBits = 24, .id = "hello"},
                                   map, {2, 1, 0}));
    EXPECT_EQ(at(img, 0, 0), (Px{0, 0, 255, 255}));
    EXPECT_EQ(at(img, 1, 0), (Px{0, 255, 0, 255}));
    EXPECT_EQ(at(img, 2, 0), (Px{255, 0, 0, 255}));
}

TEST(Tga, ColourIndexOutsideTheMapThrows) {
    const Bytes map = {1, 2, 3, 4, 5, 6};
    EXPECT_THROW(decodeTga(tga({.type = 1, .w = 1, .h = 1, .bpp = 8, .mapType = 1, .mapFirst = 1, .mapLen = 2,
                                .mapBits = 24},
                               map, {0})),  // below mapFirst
                 ContentError);
    EXPECT_THROW(decodeTga(tga({.type = 1, .w = 1, .h = 1, .bpp = 8, .mapType = 1, .mapFirst = 0, .mapLen = 2,
                                .mapBits = 24},
                               map, {2})),
                 ContentError);
    // Colour-mapped without a map, or with 16-bit indices.
    EXPECT_THROW(decodeTga(tga({.type = 1, .w = 1, .h = 1, .bpp = 8}, {}, {0})), ContentError);
    EXPECT_THROW(decodeTga(tga({.type = 1, .w = 1, .h = 1, .bpp = 16, .mapType = 1, .mapLen = 2, .mapBits = 24},
                               map, {0, 0})),
                 ContentError);
}

TEST(Tga, TrueColourImageWithAColourMapSkipsIt) {
    const auto img = decodeTga(tga({.type = 2, .w = 1, .h = 1, .bpp = 24, .mapType = 1, .mapLen = 2, .mapBits = 24},
                                   {9, 9, 9, 9, 9, 9}, {1, 2, 3}));
    EXPECT_EQ(at(img, 0, 0), (Px{3, 2, 1, 255}));
}

TEST(Tga, Type3Grey) {
    const auto img = decodeTga(tga({.type = 3, .w = 2, .h = 1, .bpp = 8, .desc = 8}, {}, {0, 200}));
    EXPECT_EQ(at(img, 0, 0), (Px{0, 0, 0, 255}));
    EXPECT_EQ(at(img, 1, 0), (Px{200, 200, 200, 255}));
}

TEST(Tga, Type10RlePacketsMayCrossRows) {
    // 3x2, 24 bpp: run of 4 red (crosses from the bottom row into the top row), then 2 raw pixels.
    const Bytes rle = {0x83, 0, 0, 255, /* raw 2: */ 0x01, 255, 0, 0, 0, 255, 0};
    const auto img = decodeTga(tga({.type = 10, .w = 3, .h = 2, .bpp = 24}, {}, rle));
    EXPECT_EQ(at(img, 0, 1), (Px{255, 0, 0, 255}));  // bottom row, stored first
    EXPECT_EQ(at(img, 2, 1), (Px{255, 0, 0, 255}));
    EXPECT_EQ(at(img, 0, 0), (Px{255, 0, 0, 255}));  // 4th run pixel = top row start
    EXPECT_EQ(at(img, 1, 0), (Px{0, 0, 255, 255}));
    EXPECT_EQ(at(img, 2, 0), (Px{0, 255, 0, 255}));
}

TEST(Tga, Type10Rle32AndType9And11) {
    const auto rgba = decodeTga(tga({.type = 10, .w = 2, .h = 1, .bpp = 32, .desc = 0x28}, {}, {0x81, 1, 2, 3, 4}));
    EXPECT_EQ(at(rgba, 1, 0), (Px{3, 2, 1, 4}));
    const auto grey = decodeTga(tga({.type = 11, .w = 3, .h = 1, .bpp = 8, .desc = 0x20}, {}, {0x82, 77}));
    EXPECT_EQ(at(grey, 2, 0), (Px{77, 77, 77, 255}));
    const auto mapped = decodeTga(tga({.type = 9, .w = 2, .h = 1, .bpp = 8, .desc = 0x20, .mapType = 1, .mapLen = 1,
                                       .mapBits = 24},
                                      {10, 20, 30}, {0x81, 0}));
    EXPECT_EQ(at(mapped, 1, 0), (Px{30, 20, 10, 255}));
}

TEST(Tga, RlePacketPastTheImageEndThrows) {
    EXPECT_THROW(decodeTga(tga({.type = 10, .w = 2, .h = 1, .bpp = 24}, {}, {0x82, 1, 2, 3})), ContentError);
}

TEST(Tga, BadHeadersThrow) {
    EXPECT_THROW(decodeTga(tga({.type = 0}, {}, {0, 0, 0})), ContentError);
    EXPECT_THROW(decodeTga(tga({.type = 32}, {}, {0, 0, 0})), ContentError);
    EXPECT_THROW(decodeTga(tga({.type = 2, .bpp = 12}, {}, {0, 0, 0})), ContentError);
    EXPECT_THROW(decodeTga(tga({.type = 3, .bpp = 16}, {}, {0, 0})), ContentError);
    EXPECT_THROW(decodeTga(tga({.type = 2, .w = 0}, {}, {0, 0, 0})), ContentError);
    EXPECT_THROW(decodeTga(tga({.type = 2, .h = 0}, {}, {0, 0, 0})), ContentError);
    EXPECT_THROW(decodeTga(tga({.type = 2, .mapType = 2}, {}, {0, 0, 0})), ContentError);
}

TEST(Tga, HugeSizesThrowBeforeAllocating) {
    // 9000 is over the 8192 limit; 65535x65535 too. Neither has data to back it.
    EXPECT_THROW(decodeTga(tga({.type = 2, .w = 9000, .h = 1}, {}, {1, 2, 3})), ContentError);
    EXPECT_THROW(decodeTga(tga({.type = 10, .w = 65535, .h = 65535, .bpp = 32}, {}, {0xff, 1, 2, 3, 4})),
                 ContentError);
    // Allowed size, but a few bytes can't hold 8192x8192 even as RLE.
    EXPECT_THROW(decodeTga(tga({.type = 10, .w = 8192, .h = 8192, .bpp = 32}, {}, {0xff, 1, 2, 3, 4})),
                 ContentError);
}

TEST(Tga, TruncatedThrows) {
    // Without the footer, every shorter prefix lacks pixel data.
    everyPrefixThrows(tga({.type = 2, .w = 2, .h = 2, .bpp = 24, .id = "ab"}, {}, Bytes(12, 7), false),
                      [](const Bytes& b) { return decodeTga(b); });
    everyPrefixThrows(tga({.type = 10, .w = 3, .h = 2, .bpp = 24}, {}, {0x83, 0, 0, 255, 0x01, 255, 0, 0, 0, 255, 0},
                          false),
                      [](const Bytes& b) { return decodeTga(b); });
    everyPrefixThrows(tga({.type = 1, .w = 2, .h = 1, .bpp = 8, .mapType = 1, .mapLen = 2, .mapBits = 24},
                          {1, 2, 3, 4, 5, 6}, {0, 1}, false),
                      [](const Bytes& b) { return decodeTga(b); });
}

TEST(Tga, MutatedFilesNeverCrash) {
    mutationsNeverCrash(tga({.type = 10, .w = 3, .h = 2, .bpp = 24}, {}, {0x83, 0, 0, 255, 0x01, 255, 0, 0, 0, 255, 0}),
                        [](const Bytes& b) { return decodeTga(b); });
    mutationsNeverCrash(tga({.type = 1, .w = 2, .h = 1, .bpp = 8, .mapType = 1, .mapLen = 2, .mapBits = 24},
                            {1, 2, 3, 4, 5, 6}, {0, 1}),
                        [](const Bytes& b) { return decodeTga(b); });
}

// ---------------------------------------------------------------- BMP builders

namespace {

struct BmpHead {
    std::int32_t w = 1, h = 1;
    std::uint16_t bpp = 24;
    std::uint32_t compression = 0;
    std::uint32_t colorsUsed = 0;
    std::uint32_t headerSize = 40;
    std::uint16_t planes = 1;
};

Bytes bmp(const BmpHead& t, const std::vector<Px>& paletteBgrx, const Bytes& pixels) {
    Bytes b = {'B', 'M'};
    const std::uint32_t offset = 14 + t.headerSize + static_cast<std::uint32_t>(paletteBgrx.size()) * 4;
    putU32(b, offset + static_cast<std::uint32_t>(pixels.size()));
    putU32(b, 0);
    putU32(b, offset);
    putU32(b, t.headerSize);
    putU32(b, static_cast<std::uint32_t>(t.w));
    putU32(b, static_cast<std::uint32_t>(t.h));
    putU16(b, t.planes);
    putU16(b, t.bpp);
    putU32(b, t.compression);
    putU32(b, static_cast<std::uint32_t>(pixels.size()));
    putU32(b, 2835);
    putU32(b, 2835);
    putU32(b, t.colorsUsed);
    putU32(b, 0);
    while (b.size() < 14 + t.headerSize) {
        b.push_back(0);
    }
    for (const auto& c : paletteBgrx) {
        b.insert(b.end(), c.begin(), c.end());
    }
    b.insert(b.end(), pixels.begin(), pixels.end());
    return b;
}

// 3x2, 24 bpp, bottom-up; rows padded from 9 to 12 bytes.
Bytes bmp24() {
    return bmp({.w = 3, .h = 2}, {},
               {0, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0,          // bottom: red, green, blue
                255, 255, 255, 0, 0, 0, 128, 128, 128, 0, 0, 0});  // top: white, black, grey
}

const std::vector<Px> kGreyPalette4 = {{0, 0, 0, 0}, {85, 85, 85, 0}, {170, 170, 170, 0}, {255, 255, 255, 0}};

}  // namespace

TEST(Bmp, Rgb24BottomUpWithRowPadding) {
    const auto img = decodeBmp(bmp24());
    ASSERT_EQ(img.width, 3u);
    ASSERT_EQ(img.height, 2u);
    EXPECT_EQ(at(img, 0, 0), (Px{255, 255, 255, 255}));
    EXPECT_EQ(at(img, 2, 0), (Px{128, 128, 128, 255}));
    EXPECT_EQ(at(img, 0, 1), (Px{255, 0, 0, 255}));
    EXPECT_EQ(at(img, 1, 1), (Px{0, 255, 0, 255}));
    EXPECT_EQ(at(img, 2, 1), (Px{0, 0, 255, 255}));
    EXPECT_FALSE(img.alphaFromFile);
}

TEST(Bmp, TopDown) {
    const auto img = decodeBmp(bmp({.w = 1, .h = -2}, {}, {0, 0, 255, 0, 255, 0, 0, 0}));
    EXPECT_EQ(at(img, 0, 0), (Px{255, 0, 0, 255}));  // first stored row is the top
    EXPECT_EQ(at(img, 0, 1), (Px{0, 0, 255, 255}));
}

TEST(Bmp, Rgb32ReservedByteIsIgnored) {
    const auto img = decodeBmp(bmp({.w = 1, .h = 1, .bpp = 32}, {}, {1, 2, 3, 0}));
    EXPECT_EQ(at(img, 0, 0), (Px{3, 2, 1, 255}));
}

TEST(Bmp, LargerInfoHeadersWork) {
    for (std::uint32_t hs : {52u, 56u, 108u, 124u}) {
        const auto img = decodeBmp(bmp({.w = 1, .h = 1, .headerSize = hs}, {}, {1, 2, 3, 0}));
        EXPECT_EQ(at(img, 0, 0), (Px{3, 2, 1, 255})) << hs;
    }
}

TEST(Bmp, Palette8WithAndWithoutColoursUsed) {
    // colorsUsed = 4: a short palette; 0 would mean 256 entries.
    const auto img = decodeBmp(bmp({.w = 2, .h = 1, .bpp = 8, .colorsUsed = 4}, kGreyPalette4, {3, 1, 0, 0}));
    EXPECT_EQ(at(img, 0, 0), (Px{255, 255, 255, 255}));
    EXPECT_EQ(at(img, 1, 0), (Px{85, 85, 85, 255}));
    std::vector<Px> full(256, Px{0, 0, 0, 0});
    full[200] = {10, 20, 30, 0};
    const auto img2 = decodeBmp(bmp({.w = 1, .h = 1, .bpp = 8}, full, {200, 0, 0, 0}));
    EXPECT_EQ(at(img2, 0, 0), (Px{30, 20, 10, 255}));
    EXPECT_TRUE(img2.warnings.empty());
}

TEST(Bmp, IndexPastAShortPaletteWarnsAndDrawsBlack) {
    const auto img = decodeBmp(bmp({.w = 1, .h = 1, .bpp = 8, .colorsUsed = 4}, kGreyPalette4, {9, 0, 0, 0}));
    EXPECT_EQ(at(img, 0, 0), (Px{0, 0, 0, 255}));
    EXPECT_EQ(img.warnings.size(), 1u);
}

TEST(Bmp, Palette4And1) {
    const auto p4 = decodeBmp(bmp({.w = 3, .h = 1, .bpp = 4, .colorsUsed = 4}, kGreyPalette4, {0x31, 0x20, 0, 0}));
    EXPECT_EQ(at(p4, 0, 0), (Px{255, 255, 255, 255}));
    EXPECT_EQ(at(p4, 1, 0), (Px{85, 85, 85, 255}));
    EXPECT_EQ(at(p4, 2, 0), (Px{170, 170, 170, 255}));
    const auto p1 = decodeBmp(bmp({.w = 9, .h = 1, .bpp = 1}, {{0, 0, 0, 0}, {255, 255, 255, 0}}, {0x81, 0x80, 0, 0}));
    EXPECT_EQ(at(p1, 0, 0), (Px{255, 255, 255, 255}));
    EXPECT_EQ(at(p1, 1, 0), (Px{0, 0, 0, 255}));
    EXPECT_EQ(at(p1, 7, 0), (Px{255, 255, 255, 255}));
    EXPECT_EQ(at(p1, 8, 0), (Px{255, 255, 255, 255}));
}

TEST(Bmp, Rle8) {
    // 4x3: bottom row "run of 4 x idx1", EOL; middle row absolute [2,3,1] + pad, EOL;
    // delta (+2,+0) along the top row, run of 1 x idx3, end of bitmap.
    const Bytes rle = {4, 1, 0, 0, /* absolute 3: */ 0, 3, 2, 3, 1, 0, 0, 0, /* delta: */ 0, 2, 2, 0, 1, 3, 0, 1};
    const auto img = decodeBmp(bmp({.w = 4, .h = 3, .bpp = 8, .compression = 1, .colorsUsed = 4}, kGreyPalette4, rle));
    EXPECT_EQ(at(img, 0, 2), (Px{85, 85, 85, 255}));
    EXPECT_EQ(at(img, 3, 2), (Px{85, 85, 85, 255}));
    EXPECT_EQ(at(img, 0, 1), (Px{170, 170, 170, 255}));
    EXPECT_EQ(at(img, 1, 1), (Px{255, 255, 255, 255}));
    EXPECT_EQ(at(img, 2, 1), (Px{85, 85, 85, 255}));
    EXPECT_EQ(at(img, 3, 1), (Px{0, 0, 0, 255}));  // skipped by EOL: index 0
    EXPECT_EQ(at(img, 0, 0), (Px{0, 0, 0, 255}));  // skipped by the delta
    EXPECT_EQ(at(img, 3, 0), (Px{0, 0, 0, 255}));  // never written
    EXPECT_EQ(at(img, 2, 0), (Px{255, 255, 255, 255}));
    EXPECT_TRUE(img.warnings.empty());
}

TEST(Bmp, Rle8OverrunWarnsAndRunningPastTheLastRowThrows) {
    const auto img = decodeBmp(bmp({.w = 2, .h = 1, .bpp = 8, .compression = 1, .colorsUsed = 4}, kGreyPalette4,
                                   {5, 1, 0, 1}));
    EXPECT_EQ(img.warnings.size(), 1u);
    EXPECT_THROW(decodeBmp(bmp({.w = 2, .h = 1, .bpp = 8, .compression = 1, .colorsUsed = 4}, kGreyPalette4,
                               {0, 0, 0, 0, 0, 1})),
                 ContentError);
    // No end-of-bitmap marker: runs out of data.
    EXPECT_THROW(decodeBmp(bmp({.w = 2, .h = 1, .bpp = 8, .compression = 1, .colorsUsed = 4}, kGreyPalette4,
                               {2, 1})),
                 ContentError);
}

TEST(Bmp, BadHeadersThrow) {
    Bytes noMagic = bmp24();
    noMagic[0] = 'X';
    EXPECT_THROW(decodeBmp(noMagic), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.headerSize = 12}, {}, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.bpp = 16}, {}, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.compression = 3}, {}, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.bpp = 4, .compression = 2}, kGreyPalette4, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.planes = 2}, {}, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.w = 0}, {}, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.w = -1}, {}, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.h = 0}, {}, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.h = INT32_MIN}, {}, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.bpp = 4, .colorsUsed = 17}, kGreyPalette4, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.h = -1, .bpp = 8, .compression = 1, .colorsUsed = 4}, kGreyPalette4, {0, 1})),
                 ContentError);
    Bytes badOffset = bmp24();
    setU32(badOffset, 10, 0x7fffffff);
    EXPECT_THROW(decodeBmp(badOffset), ContentError);
}

TEST(Bmp, HugeSizesThrowBeforeAllocating) {
    EXPECT_THROW(decodeBmp(bmp({.w = 8193, .h = 1}, {}, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.w = 0x7fffffff, .h = 0x7fffffff}, {}, {0, 0, 0, 0})), ContentError);
    EXPECT_THROW(decodeBmp(bmp({.w = 8192, .h = -8192, .bpp = 32}, {}, {0, 0, 0, 0})), ContentError);
}

TEST(Bmp, TruncatedThrows) {
    everyPrefixThrows(bmp24(), [](const Bytes& b) { return decodeBmp(b); });
    everyPrefixThrows(bmp({.w = 3, .h = 1, .bpp = 4, .colorsUsed = 4}, kGreyPalette4, {0x31, 0x20, 0, 0}),
                      [](const Bytes& b) { return decodeBmp(b); });
    everyPrefixThrows(bmp({.w = 4, .h = 3, .bpp = 8, .compression = 1, .colorsUsed = 4}, kGreyPalette4,
                          {4, 1, 0, 0, 0, 3, 2, 3, 1, 0, 0, 0, 0, 2, 2, 0, 1, 3, 0, 1}),
                      [](const Bytes& b) { return decodeBmp(b); });
}

TEST(Bmp, MutatedFilesNeverCrash) {
    mutationsNeverCrash(bmp24(), [](const Bytes& b) { return decodeBmp(b); });
    mutationsNeverCrash(bmp({.w = 4, .h = 3, .bpp = 8, .compression = 1, .colorsUsed = 4}, kGreyPalette4,
                            {4, 1, 0, 0, 0, 3, 2, 3, 1, 0, 0, 0, 0, 2, 2, 0, 1, 3, 0, 1}),
                        [](const Bytes& b) { return decodeBmp(b); });
}

// ---------------------------------------------------------------- JPEG

namespace {

bool requireJpeg() {
    const char* v = std::getenv("TRAINZ_REQUIRE_JPEG");
    return v != nullptr && std::string(v) == "1";
}

#define SKIP_WITHOUT_JPEG()                                                                   \
    do {                                                                                      \
        if (!jpegSupported()) {                                                               \
            if (requireJpeg()) {                                                              \
                FAIL() << "TRAINZ_REQUIRE_JPEG=1 but the build has no JPEG support";          \
            }                                                                                 \
            GTEST_SKIP() << "built without libjpeg (install libjpeg62-turbo-dev)";            \
        }                                                                                     \
    } while (0)

#if TRAINZ_HAVE_JPEG
// Encodes a w x h test picture (left half red, right half blue; or a grey ramp) with libjpeg.
Bytes encodeJpeg(unsigned w, unsigned h, bool grey, bool progressive) {
    jpeg_compress_struct c{};
    jpeg_error_mgr err{};
    c.err = jpeg_std_error(&err);
    jpeg_CreateCompress(&c, JPEG_LIB_VERSION, sizeof(jpeg_compress_struct));
    unsigned char* out = nullptr;
    unsigned long outSize = 0;
    jpeg_mem_dest(&c, &out, &outSize);
    c.image_width = w;
    c.image_height = h;
    c.input_components = grey ? 1 : 3;
    c.in_color_space = grey ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&c);
    jpeg_set_quality(&c, 95, TRUE);
    if (progressive) {
        jpeg_simple_progression(&c);
    }
    jpeg_start_compress(&c, TRUE);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(w) * 3);
    while (c.next_scanline < h) {
        for (unsigned x = 0; x < w; ++x) {
            if (grey) {
                row[x] = static_cast<std::uint8_t>(x * 255 / (w - 1));
            } else {
                const bool left = x < w / 2;
                row[3 * x] = left ? 255 : 0;
                row[3 * x + 1] = 0;
                row[3 * x + 2] = left ? 0 : 255;
            }
        }
        JSAMPROW r = row.data();
        jpeg_write_scanlines(&c, &r, 1);
    }
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    Bytes b(out, out + outSize);
    std::free(out);
    return b;
}
#else
Bytes encodeJpeg(unsigned, unsigned, bool, bool) { return {}; }
#endif

bool near(std::uint8_t a, int b) { return std::abs(int{a} - b) <= 12; }

}  // namespace

TEST(Jpeg, WithoutLibjpegItThrows) {
    if (jpegSupported()) {
        GTEST_SKIP() << "built with libjpeg";
    }
    EXPECT_THROW(decodeJpeg(Bytes{0xFF, 0xD8, 0xFF, 0xD9}), ContentError);
}

TEST(Jpeg, BaselineAndProgressiveColour) {
    SKIP_WITHOUT_JPEG();
    for (bool progressive : {false, true}) {
        const auto img = decodeJpeg(encodeJpeg(32, 16, false, progressive));
        ASSERT_EQ(img.width, 32u);
        ASSERT_EQ(img.height, 16u);
        const Px l = at(img, 4, 8);
        const Px r = at(img, 28, 8);
        EXPECT_TRUE(near(l[0], 255) && near(l[1], 0) && near(l[2], 0)) << progressive;
        EXPECT_TRUE(near(r[0], 0) && near(r[1], 0) && near(r[2], 255)) << progressive;
        EXPECT_EQ(l[3], 255);
        EXPECT_FALSE(img.alphaFromFile);
        EXPECT_NE(img.description.find(progressive ? "progressive" : "baseline"), std::string::npos);
        EXPECT_TRUE(img.warnings.empty());
    }
}

TEST(Jpeg, Grey) {
    SKIP_WITHOUT_JPEG();
    const auto img = decodeJpeg(encodeJpeg(64, 8, true, false));
    EXPECT_TRUE(near(at(img, 0, 4)[0], 0));
    EXPECT_TRUE(near(at(img, 63, 4)[2], 255));
    EXPECT_NE(img.description.find("1 component"), std::string::npos);
}

TEST(Jpeg, MissingEndMarkerOnlyWarns) {
    SKIP_WITHOUT_JPEG();
    Bytes b = encodeJpeg(32, 16, false, false);
    ASSERT_EQ(b[b.size() - 2], 0xFF);
    ASSERT_EQ(b[b.size() - 1], 0xD9);
    b.resize(b.size() - 2);
    const auto img = decodeJpeg(b);
    EXPECT_EQ(img.width, 32u);
    ASSERT_EQ(img.warnings.size(), 1u);
}

TEST(Jpeg, TruncatedThrows) {
    SKIP_WITHOUT_JPEG();
    for (bool progressive : {false, true}) {
        const Bytes good = encodeJpeg(48, 48, false, progressive);
        // Every cut that removes picture data (keep the last 2 bytes = only the end marker).
        // For progressive files that includes cuts between scans: the picture would
        // decode, but blurry, so a missing scan must count as truncation too.
        for (std::size_t len = 0; len + 2 < good.size(); len += 3) {
            const Bytes cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len));
            EXPECT_THROW(decodeJpeg(cut), ContentError) << "prefix " << len << " progressive " << progressive;
        }
    }
}

TEST(Jpeg, GarbageAndBadSizesThrow) {
    SKIP_WITHOUT_JPEG();
    EXPECT_THROW(decodeJpeg(Bytes{}), ContentError);
    EXPECT_THROW(decodeJpeg(Bytes{0x89, 'P', 'N', 'G'}), ContentError);
    EXPECT_THROW(decodeJpeg(Bytes{0xFF, 0xD8, 1, 2, 3, 4, 5, 6, 7, 8}), ContentError);
    // Patch the SOF0 size fields: 0 height, then 65535 x 65535 (over the limit).
    const Bytes good = encodeJpeg(16, 16, false, false);
    std::size_t sof = 0;
    for (std::size_t i = 2; i + 9 < good.size(); ++i) {
        if (good[i] == 0xFF && good[i + 1] == 0xC0) {
            sof = i;
            break;
        }
    }
    ASSERT_NE(sof, 0u);
    Bytes zero = good;
    zero[sof + 5] = 0, zero[sof + 6] = 0;
    EXPECT_THROW(decodeJpeg(zero), ContentError);
    Bytes huge = good;
    huge[sof + 5] = huge[sof + 6] = huge[sof + 7] = huge[sof + 8] = 0xFF;
    EXPECT_THROW(decodeJpeg(huge), ContentError);
    Bytes big = good;  // 8193 wide
    big[sof + 7] = 0x20, big[sof + 8] = 0x01;
    EXPECT_THROW(decodeJpeg(big), ContentError);
}

TEST(Jpeg, MutatedFilesNeverCrash) {
    SKIP_WITHOUT_JPEG();
    mutationsNeverCrash(encodeJpeg(24, 24, false, false), [](const Bytes& b) { return decodeJpeg(b); }, 1500);
    mutationsNeverCrash(encodeJpeg(24, 24, false, true), [](const Bytes& b) { return decodeJpeg(b); }, 1500);
}
