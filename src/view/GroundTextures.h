// Ground-texture assets (kind "groundtexture") for the route view: KUID -> a small RGBA copy
// of its image with smaller mips. The image is the config's asset-filename + ".texture", or
// + ".texture.txt" (a descriptor naming a .bmp/.tga/.jpg). Observed on the disc, not traced.
// Read-only; each asset loaded once.
#pragma once

#include "content/AssetIndex.h"
#include "content/GroundFile.h"
#include "content/JaArchive.h"
#include "content/Kuid.h"
#include "view/SceneRender.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace trainz::view {

class GroundTextures {
public:
    explicit GroundTextures(const trainz::content::AssetIndex& index) : index_(index) {}

    // The texture (at most kSide x kSide, its smaller mips in `smaller`), or nullptr with
    // the reason counted in counts().
    const SceneTexture* get(const trainz::content::Kuid& kuid);
    // How each asset was read, or why it could not be.
    const std::map<std::string, std::size_t>& counts() const { return counts_; }

    static constexpr std::uint32_t kSide = 128;

private:
    std::unique_ptr<SceneTexture> load(const trainz::content::Kuid& kuid);
    const trainz::content::JaArchive* archive(const std::filesystem::path& path);

    const trainz::content::AssetIndex& index_;
    std::map<std::string, std::unique_ptr<SceneTexture>> cache_;  // by formatted KUID; null = none
    std::map<std::string, std::unique_ptr<trainz::content::JaArchive>> archives_;
    std::map<std::string, std::size_t> counts_;
};

// A box-filtered half-size copy (each side halved, at least 1). For building mips of images
// that come without them (descriptor images).
SceneTexture halveTexture(const SceneTexture& t);

// ASSUMPTION (not traced, not documented here): one repeat of a ground texture covers this
// many terrain cells (10 m each) along each axis.
inline constexpr double kGroundTileCells = 1.0;

// Bakes one terrain section's ground into a size x size image (with mips in `smaller`):
// image column i and row j cover cells (i / size) * cells .. and (j / size) * cells .. of the
// section (columns +x, rows +y, as the vertex grid). Each pixel blends the textures of the
// four surrounding vertices' layers, weighted by distance (bilinear) and by each layer's
// weight byte; `slotTexture[k]` is the texture for section slot k (nullptr = none). Pixels
// with no usable texture take `fallback(col, row)` (in cell units). `side` is the vertex grid
// side (cells = side - 4). Throws std::invalid_argument for a bad size, side or vertex count.
SceneTexture bakeGroundSection(const trainz::content::GroundSectionData& data, std::size_t side,
                               const std::vector<const SceneTexture*>& slotTexture, std::uint32_t size,
                               const std::function<Rgb(double col, double row)>& fallback,
                               double tileCells = kGroundTileCells);

}  // namespace trainz::view
