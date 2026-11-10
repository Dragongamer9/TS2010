// A tiny software renderer for checking meshes by eye (no GPU, no libraries).
// Flat-shaded, orthographic, depth-buffered. Not the game renderer.
#pragma once

#include "content/AnimationFile.h"
#include "content/MeshFile.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace trainz::view {

enum class PreviewView { Side, Front, Top, ThreeQuarter };

// Renders all parts into width x height RGBA bytes. Z is up.
std::vector<std::uint8_t> renderMeshPreview(const trainz::content::MeshFile& mesh, std::uint32_t width,
                                            std::uint32_t height, PreviewView view);

// Moves every part that names a bone (MeshGeometry::boneName) to that bone's
// pose in the given frame: v' = rotate(q, v) + position. ASSUMPTION: bone
// positions are absolute (model space); see docs/formats/animation.md.
// Parts whose bone is not in the animation are left as they are.
// Works IN PLACE: calling it again on the same mesh adds the pose again, so
// for playback always start each frame from a fresh copy of the parsed mesh.
// Returns how many parts were moved. Throws std::out_of_range for a bad
// frame, or when that frame has fewer poses than the animation has bones;
// the mesh is not changed when it throws.
std::size_t poseMesh(trainz::content::MeshFile& mesh, const trainz::content::AnimationFile& anim, std::size_t frame);

}  // namespace trainz::view
