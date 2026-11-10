// .texture.txt descriptors: a small text file naming the image(s) a texture is
// built from, plus sampling hints. See docs/formats/texture-descriptor.md.
//
// Traced in RenderSystem_7c.dll (reader around VA 0x10005aa0-0x10006500): the
// keys, the value words and the numbers the engine stores for them (tables at
// 0x1006ca78-0x1006cb34), lowercasing before comparing, and the Tile rule.
// Observed only: the line syntax ("Key=value", one per line; the game reads the
// file through Jet::ConfigData, whose tokenizer is not traced).
// From the official wiki only, not traced: how Primary and Alpha combine
// (composeDescriptorImage).
#pragma once

#include "content/ImageFile.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trainz::content {

// Each enum's numbers are the ones RenderSystem_7c.dll stores (traced).
enum class TexDimension : std::uint8_t { Two = 0, Cube = 1, Volume = 2, One = 3 };
enum class TexCompression : std::uint8_t { None = 0, Dxt1 = 1, Dxt3 = 2, Dxt5 = 3 };
enum class TexFilter : std::uint8_t { Nearest = 0, Linear = 1, Default = 2 };
enum class TexHint : std::uint8_t { Static = 0, Dynamic = 1 };
enum class TexNormalMapHint : std::uint8_t { None = 0, NormalMap = 1 };
enum class TexModifyMap : std::uint8_t { None = 0, FlipGreen = 1 };
enum class TexAlphaHint : std::uint8_t { Opaque = 0, SemiTransparent = 1, Masked = 2 };
enum class TexAutoMip : std::uint8_t { None = 0, Default = 1, Fastest = 2, Nicest = 3 };

// A field left empty means the key was absent or its word unknown: the game then
// keeps its default, which has not been traced.
struct TextureDescriptor {
    std::vector<std::string> primary;  // file names as written, in file order
    std::vector<std::string> alpha;
    std::optional<TexDimension> dimension;
    std::optional<TexCompression> compression;
    std::optional<TexHint> hint;
    std::optional<TexNormalMapHint> normalMapHint;
    std::optional<TexModifyMap> modifyMap;
    std::optional<TexAlphaHint> alphaHint;
    std::optional<std::int32_t> anisotropy;
    std::optional<TexAutoMip> autoMip;
    std::optional<TexFilter> magFilter;
    std::optional<TexFilter> minFilter;
    std::optional<TexFilter> mipFilter;
    // Tile (traced): present means clamp both ways unless the lowercased value
    // contains 's' (wrap horizontally) or 't' (wrap vertically). "st" wraps both,
    // "none" clamps both. Empty when there is no Tile key.
    std::optional<bool> wrapS;
    std::optional<bool> wrapT;
    std::vector<std::string> warnings;
};

// Never throws on odd content: unknown keys or words become warnings. Throws
// ContentError only for input that can't be a descriptor (binary data, over 64 KiB).
TextureDescriptor parseTextureDescriptor(std::span<const std::uint8_t> data);

// Where a Primary/Alpha name points: the descriptor's own folder plus the name.
// (The game strips the descriptor's file name with File::Path::DelPath and then
// calls Resource::ConvertResourceName, which is not traced.)
std::string resolveDescriptorPath(std::string_view descriptorName, std::string_view imageName);

// Builds the texture's RGBA image, following the official wiki
// (online.ts2009.com "Texture file"; not traced in the game):
// - no Alpha: Primary's colour, opaque (any alpha in the file is ignored);
// - Alpha names the same file as Primary: that image's RGBA as is;
// - a different Alpha image: Primary's colour, opacity from the Alpha image's
//   grey level (its red channel; exact for grey images). Sizes must match, or
//   this throws ContentError.
RgbaImage composeDescriptorImage(const RgbaImage& primary, const RgbaImage* alpha, bool alphaIsSameFile);

}  // namespace trainz::content
