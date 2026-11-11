// Loads an asset's 3D model for the route view: KUID -> asset (AssetIndex) -> config's
// mesh -> triangles, each part coloured by the average colour of its first texture. A
// preview helper, not the game's renderer: no textures on the surfaces, no LODs, no
// animation. Read-only; every asset is loaded at most once.
#pragma once

#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/Kuid.h"
#include "view/SceneRender.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace trainz::view {

struct LocalTriangle {
    float p[3][3];  // model space, metres, Z up
    Rgb colour;
};

struct PreparedMesh {
    std::string meshName;  // the file used, inside the asset folder
    std::vector<LocalTriangle> triangles;
};

class AssetMeshes {
public:
    explicit AssetMeshes(const trainz::content::AssetIndex& index) : index_(index) {}

    // The asset's model, or nullptr if it has none the port can read (no mesh entry, only
    // a .lm with no .im beside it, damaged files, over the triangle limit). Why is recorded.
    const PreparedMesh* get(const trainz::content::Kuid& kuid);

    // Counts of why assets had no model, for reporting.
    const std::map<std::string, std::size_t>& misses() const { return misses_; }

    static constexpr std::size_t kMaxTrianglesPerMesh = 60000;

private:
    const trainz::content::JaArchive* archive(const std::filesystem::path& path);
    std::unique_ptr<PreparedMesh> load(const trainz::content::Kuid& kuid);

    const trainz::content::AssetIndex& index_;
    std::map<std::string, std::unique_ptr<PreparedMesh>> cache_;  // by formatted KUID; null = none
    std::map<std::string, std::unique_ptr<trainz::content::JaArchive>> archives_;
    std::map<std::string, std::size_t> misses_;
};

// Places a prepared mesh in the world: rotate by yaw (radians, anticlockwise, about +z)
// and move to (x, y, z). Appends to the scene.
void addPlacedMesh(Scene& scene, const PreparedMesh& mesh, double x, double y, double z, double yaw);

}  // namespace trainz::view
