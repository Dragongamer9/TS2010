// Loose image files referenced by assets: .tga, .bmp, .jpg / .jpeg.
// See docs/formats/image.md.
//
// The decoders cover every sub-type found on the TS2010 disc (all 8 Builtin
// archives plus Base.ja and Data.ja, 20,544 images; survey 2026-11-10) and a
// few close relatives from the public format specs. Nothing here is traced in
// the game's code: the layouts come from the public TGA 2.0 and BMP specs, and
// JPEG goes through libjpeg (libjpeg62-turbo).
//
// Every decoder throws ContentError on damaged or unsupported data, checks
// sizes (1..kMaxTextureSize per side) before any arithmetic, and never reads
// outside the input.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trainz::content {

// The same layout decodeToRgba8() (TextureFile.h) returns: 8-bit R, G, B, A
// bytes, row by row, top row first, with the size alongside.
struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4 bytes
    // True when A comes from the file (TGA 32 bpp, 16 bpp with an alpha bit, or a
    // colour map with alpha); false when A is just 255. On the disc, 165 TGAs that
    // carry alpha have it 0 everywhere, so whether alpha is used must come from
    // the material, not from this flag (see docs/formats/image.md).
    bool alphaFromFile = false;
    std::string description;         // e.g. "TGA type 10 (RLE true-colour), 24 bpp, bottom-left origin"
    std::vector<std::string> warnings;
};

enum class ImageKind { Unknown, Tga, Bmp, Jpeg };

const char* imageKindName(ImageKind k);

// From the file name's extension (case-insensitive): .tga, .bmp, .jpg, .jpeg.
ImageKind imageKindFromName(std::string_view name);

// TGA image types 1/9 (colour-mapped, 8-bit index), 2/10 (true-colour 15/16/24/32
// bpp) and 3/11 (grey 8 bpp); either origin, right-to-left too.
RgbaImage decodeTga(std::span<const std::uint8_t> data);

// Windows BMP with a 40-byte (or larger V4/V5) info header: 1/4/8 bpp with a
// palette, 24 and 32 bpp uncompressed, 8 bpp RLE8; bottom-up or top-down.
RgbaImage decodeBmp(std::span<const std::uint8_t> data);

// Baseline and progressive JPEG, grey or YCbCr, through libjpeg. Throws
// ContentError if the build has no JPEG support (see jpegSupported()).
RgbaImage decodeJpeg(std::span<const std::uint8_t> data);

// True when the library was built with libjpeg (TRAINZ_HAVE_JPEG).
bool jpegSupported();

// Picks the decoder from the kind.
RgbaImage decodeImage(ImageKind kind, std::span<const std::uint8_t> data);

}  // namespace trainz::content
