#include "content/ContentError.h"
#include "content/TextureDescriptor.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace trainz::content;

namespace {

TextureDescriptor parse(const std::string& s) {
    return parseTextureDescriptor(std::vector<std::uint8_t>(s.begin(), s.end()));
}

RgbaImage solid(std::uint32_t w, std::uint32_t h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    RgbaImage img;
    img.width = w;
    img.height = h;
    for (std::uint32_t i = 0; i < w * h; ++i) {
        img.rgba.insert(img.rgba.end(), {r, g, b, a});
    }
    return img;
}

}  // namespace

TEST(TextureDescriptor, ParsesTheDiscSyntax) {
    // A real file from Base.ja (CR LF, mixed-case keys).
    const auto d = parse(
        "Primary=Grid.tga\r\nAlpha=Grid.tga\r\nTile=st\r\nAlphaHint=semitransparent\r\nAnisotropy=16\r\n"
        "MagFilter=linear\r\nMinFilter=linear\r\nMipFilter=linear\r\nCompression=none\r\n");
    EXPECT_EQ(d.primary, std::vector<std::string>{"Grid.tga"});
    EXPECT_EQ(d.alpha, std::vector<std::string>{"Grid.tga"});
    EXPECT_EQ(d.wrapS, true);
    EXPECT_EQ(d.wrapT, true);
    EXPECT_EQ(d.alphaHint, TexAlphaHint::SemiTransparent);
    EXPECT_EQ(d.anisotropy, 16);
    EXPECT_EQ(d.magFilter, TexFilter::Linear);
    EXPECT_EQ(d.compression, TexCompression::None);
    EXPECT_FALSE(d.dimension.has_value());
    EXPECT_TRUE(d.warnings.empty());
}

TEST(TextureDescriptor, KeysAndWordsIgnoreCaseAndNoFinalNewline) {
    const auto d = parse("\xEF\xBB\xBFprimary=a.tga\nTILE=None\nhint=DYNAMIC\nDimension=cube");
    EXPECT_EQ(d.primary.at(0), "a.tga");
    EXPECT_EQ(d.wrapS, false);
    EXPECT_EQ(d.wrapT, false);
    EXPECT_EQ(d.hint, TexHint::Dynamic);
    EXPECT_EQ(d.dimension, TexDimension::Cube);
}

TEST(TextureDescriptor, TileIsALetterSearchLikeTheGame) {
    EXPECT_EQ(parse("Primary=a.tga\nTile=s\n").wrapT, false);
    EXPECT_EQ(parse("Primary=a.tga\nTile=s\n").wrapS, true);
    EXPECT_EQ(parse("Primary=a.tga\nTile=t\n").wrapT, true);
    // The game searches for the letters, so any word containing s and t wraps both.
    EXPECT_EQ(parse("Primary=a.tga\nTile=test\n").wrapS, true);
    EXPECT_FALSE(parse("Primary=a.tga\n").wrapS.has_value());
}

TEST(TextureDescriptor, EnumNumbersMatchTheEngine) {
    EXPECT_EQ(static_cast<int>(TexDimension::One), 3);
    EXPECT_EQ(static_cast<int>(TexCompression::Dxt5), 3);
    EXPECT_EQ(static_cast<int>(TexFilter::Default), 2);
    EXPECT_EQ(static_cast<int>(TexAlphaHint::Masked), 2);
    EXPECT_EQ(static_cast<int>(TexAutoMip::Nicest), 3);
}

TEST(TextureDescriptor, OddContentWarnsNotThrows) {
    const auto d = parse("Alpha=x.bmp\nMagFilter=bilinear\nAnisotropy=lots\nColour=red\n\n// note\n");
    EXPECT_FALSE(d.magFilter.has_value());
    EXPECT_FALSE(d.anisotropy.has_value());
    EXPECT_EQ(d.warnings.size(), 4U);  // bilinear, lots, Colour, no Primary
    EXPECT_THROW(parse(std::string("Primary=a\0b", 11)), ContentError);
    EXPECT_THROW(parse(std::string(70000, 'x')), ContentError);
}

TEST(TextureDescriptor, PathsAreRelativeToTheDescriptor) {
    EXPECT_EQ(resolveDescriptorPath("hash-12/kuid 1 2/body.texture.txt", "body.tga"), "hash-12/kuid 1 2/body.tga");
    EXPECT_EQ(resolveDescriptorPath("a\\b.texture.txt", "c.bmp"), "a\\c.bmp");
    EXPECT_EQ(resolveDescriptorPath("top.texture.txt", "c.bmp"), "c.bmp");
}

TEST(TextureDescriptor, ComposeFollowsTheWikiRules) {
    const RgbaImage colour = solid(2, 1, 10, 20, 30, 0);  // alpha 0 everywhere, as on 165 disc TGAs
    const auto opaque = composeDescriptorImage(colour, nullptr, false);
    EXPECT_EQ(opaque.rgba[3], 255);
    EXPECT_FALSE(opaque.alphaFromFile);

    const auto same = composeDescriptorImage(colour, &colour, true);
    EXPECT_EQ(same.rgba[3], 0);

    const RgbaImage mask = solid(2, 1, 128, 128, 128, 255);
    const auto masked = composeDescriptorImage(colour, &mask, false);
    EXPECT_EQ(masked.rgba[0], 10);
    EXPECT_EQ(masked.rgba[3], 128);
    EXPECT_TRUE(masked.alphaFromFile);

    const RgbaImage wrongSize = solid(1, 1, 0, 0, 0, 255);
    EXPECT_THROW(composeDescriptorImage(colour, &wrongSize, false), ContentError);
}
