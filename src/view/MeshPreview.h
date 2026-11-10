// A tiny software renderer for checking meshes by eye (no GPU, no libraries).
// Flat-shaded, orthographic, depth-buffered. Not the game renderer.
#pragma once

#include "content/MeshFile.h"

#include <cstdint>
#include <vector>

namespace trainz::view {

enum class PreviewView { Side, Front, Top, ThreeQuarter };

// Renders all parts into width x height RGBA bytes. Z is up.
std::vector<std::uint8_t> renderMeshPreview(const trainz::content::MeshFile& mesh, std::uint32_t width,
                                            std::uint32_t height, PreviewView view);

}  // namespace trainz::view
