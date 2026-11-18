// .texture files: JIRF form "MMTF" with an INFO chunk and one MMAP chunk per
// mip level (stored smallest first). See docs/formats/texture.md.
//
// Based on RenderSystem_7c.dll (Jet::TextureResource::Load at 0x10045500,
// SaveMipMap 0x100464f0) and checked on all 6,666 .texture files in Base.ja,
// Data.ja and Builtin\8.ja: every mip size matches the format below.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace trainz::content {

// Jet's DisplayFormat ids. The order is the engine's own list of format names
// in RenderSystem_7c.dll (stored to 0x100b65a8..0x100b65d4 by 0x1003c43c..).
// Seen in data: ARGB8888, RGB0888, DXT1, DXT3, DXT5. The others are decoded
// from their names only and have never been seen in a file.
enum class PixelFormat : std::uint32_t {
    Null = 0,
    ARGB8888 = 1,
    ABGR8888 = 2,
    RGB0888 = 3,
    BGR0888 = 4,
    RGB888 = 5,
    BGR888 = 6,
    ARGB4444 = 7,
    RGB565 = 8,
    RGB555 = 9,
    DXT1 = 10,
    DXT3 = 11,
    DXT5 = 12,
};

inline constexpr std::uint32_t kMaxTextureSize = 8192;

const char* pixelFormatName(PixelFormat f);

// Bytes for one w x h image in this format (DXT formats round up to 4x4
// blocks). nullopt for Null or unknown ids, and for sizes of 0 or above
// kMaxTextureSize (so the arithmetic can never overflow).
std::optional<std::size_t> imageByteSize(PixelFormat f, std::uint32_t w, std::uint32_t h);

struct TextureInfo {
    std::uint32_t version = 0;   // word 0: 0x107 (80-byte INFO) or 0x103 (60-byte INFO) seen
    std::uint32_t mipCount = 0;  // word 2
    PixelFormat format = PixelFormat::Null;  // word 4
    std::uint32_t width = 0;     // word 5
    std::uint32_t height = 0;    // word 6
    std::vector<std::uint32_t> words;  // the whole INFO block; other words' meaning is unknown
};

struct MipLevel {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> data;  // in the texture's own pixel format
};

struct TextureFile {
    TextureInfo info;
    std::vector<MipLevel> mips;  // largest first (reversed from the file order)
    std::vector<std::string> warnings;
};

// Throws ContentError on damage: wrong form, missing INFO, mip count or sizes
// that do not match the format, sizes over 8192.
TextureFile parseTexture(std::span<const std::uint8_t> data);

// Converts one mip level to 8-bit R,G,B,A bytes (row by row, top row first as stored).
// Throws ContentError for formats it cannot decode, for sizes of 0 or above
// kMaxTextureSize, and when the data does not match the size.
std::vector<std::uint8_t> decodeToRgba8(const MipLevel& mip, PixelFormat format);

}  // namespace trainz::content
