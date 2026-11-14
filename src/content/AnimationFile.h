// Jet animations: .kin (JIRF form "ANIM"). See docs/formats/animation.md.
//
// Layout worked out from the data and checked on all 135 .kin files in
// Base.ja, Data.ja and Builtin\8.ja: every chunk is used up exactly, the frame
// count matches INFO and every frame holds 7 floats per bone. The reader code
// (Render3DFoundation_7c.dll, AnimationModifier) has NOT been traced.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace trainz::content {

struct AnimationBone {
    std::string name;  // e.g. "b.r.wheel01"; mesh parts name the bone they follow (MeshGeometry::boneName)
    int parent = -1;   // index into AnimationFile::bones, -1 for a root
};

struct BonePose {
    std::array<float, 3> position{};  // see docs: most likely absolute (model space); not confirmed
    std::array<float, 4> rotation{0, 0, 0, 1};  // quaternion x, y, z, w
};

struct AnimationEvent {
    std::uint32_t frame = 0;
    std::uint32_t flags = 0;  // 4 or 0 seen; meaning unknown
    std::string name;         // e.g. "warble", "/warble", "warble_active"
};

struct AnimationFile {
    std::string sourceFile;           // artist's source, e.g. "AAR0bogy205.gmax"
    std::uint32_t frameCount = 0;
    std::uint32_t framesPerSecond = 0;  // 30 in most files
    float infoValue = 0;              // 1.0 in every file; meaning unknown
    std::vector<AnimationEvent> events;
    std::vector<AnimationBone> bones;           // depth-first order, as the frames store them
    std::vector<std::vector<BonePose>> frames;  // frames[frame][bone]

    // Index of a bone by name, or -1.
    int boneIndex(const std::string& name) const;
};

AnimationFile parseAnimation(std::span<const std::uint8_t> data);

// Rotates v by the unit quaternion q (x, y, z, w).
std::array<float, 3> rotate(const std::array<float, 4>& q, const std::array<float, 3>& v);

}  // namespace trainz::content
