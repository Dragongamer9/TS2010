// Loads an asset's 3D model for the route view: KUID -> asset (AssetIndex) -> config's
// meshes -> triangles, each part coloured by the average colour of its first texture.
// A preview helper, not the game's renderer. Limits: the full-detail daytime mesh-table
// entries at the model's origin are drawn; lower LODs, night variants, entries on
// attachment points ("att") and shadow meshes are skipped (see meshesToDraw), so
// attached parts can be missing; no textures on the surfaces (one flat colour per
// part); no .lm LOD lists; no animation. Read-only; each asset loaded once.
#pragma once

#include "content/AssetConfig.h"
#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/Kuid.h"
#include "view/SceneRender.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
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
    // Counts of mesh-table entries drawn and skipped (attachments, shadows, .lm, …).
    const std::map<std::string, std::size_t>& parts() const { return parts_; }

    static constexpr std::size_t kMaxTrianglesPerMesh = 150000;  // the eligible full-detail daytime parts of one asset together

private:
    const trainz::content::JaArchive* archive(const std::filesystem::path& path);
    std::unique_ptr<PreparedMesh> load(const trainz::content::Kuid& kuid);
    std::optional<trainz::content::Kuid> reskinBase(const trainz::content::Kuid& kuid);
    // Adds one mesh file's triangles; false (and counted) if it can't be read.
    bool appendMesh(const trainz::content::JaArchive& archive, const std::string& base, std::string mesh,
                    PreparedMesh& out);

    const trainz::content::AssetIndex& index_;
    std::map<std::string, std::unique_ptr<PreparedMesh>> cache_;  // by formatted KUID; null = none
    std::map<std::string, std::unique_ptr<trainz::content::JaArchive>> archives_;
    std::map<std::string, std::size_t> misses_;
    std::map<std::string, std::size_t> parts_;
};

// The highest-detail mesh named in a .lm.txt level-of-detail list: entries are
// mesh("<level>") { name="<file>"; } and the largest level is taken as full detail
// (observed: e.g. 1.0 names the plain .im and 0.2 the "_med" one; not traced). Empty if none.
std::string highestDetailLodMesh(std::span<const std::uint8_t> text);

// Which mesh files of an asset config the view draws (paths as written, relative to the
// asset folder), with counts of what was skipped and why added to `parts`. See the
// rules in AssetMeshes.cpp; they are the port's reading of the key names, not traced.
std::vector<std::string> selectAssetMeshes(const trainz::content::ConfigNode& root,
                                           std::map<std::string, std::size_t>& parts);

// Places a prepared mesh in the world: rotate by yaw (radians, anticlockwise, about +z)
// and move to (x, y, z). Appends to the scene.
void addPlacedMesh(Scene& scene, const PreparedMesh& mesh, double x, double y, double z, double yaw);

}  // namespace trainz::view
