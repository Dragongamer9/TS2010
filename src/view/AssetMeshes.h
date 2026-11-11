// Loads an asset's 3D model for the route view: KUID -> asset (AssetIndex) -> config's
// meshes -> triangles, each part textured with a small copy of its colour map (type-1
// texture), or a flat grey when it has none the port can read.
// A preview helper, not the game's renderer. Limits: the full-detail daytime mesh-table
// entries at the model's origin are drawn; lower LODs, night variants, entries on
// attachment points ("att") are hung on their parent's ATCH point; shadow meshes are
// skipped (see selectAssetMeshes). No lighting maps, no blending (cut-out alpha only, see
// appendMesh); no animation. SpeedTree trees (.srt) are not read: they get placeholderTree().
// Read-only; each asset loaded once.
#pragma once

#include "content/AssetConfig.h"
#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/Kuid.h"
#include "content/MeshFile.h"
#include "view/SceneRender.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace trainz::view {

// A rigid placement inside a model: p' = p.x * axis x + p.y * axis y + p.z * axis z + origin,
// with the axes as r[0..2], r[3..5], r[6..8] (the index order assumed for ATCH, see MeshFile.h).
struct LocalFrame {
    float r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float t[3] = {0, 0, 0};

    static LocalFrame of(const trainz::content::MeshAttachment& a);
    void apply(const float in[3], float out[3]) const;
    // This frame, then `outer` (a point inside this frame, seen from outer's parent).
    LocalFrame then(const LocalFrame& outer) const;
};

// The frame `point` turned by `turn` radians about the model's own vertical (z) axis through
// the point's position: e.g. a bogie swivelling under its body. The point's position stays,
// and a tilted point keeps its tilt relative to that axis.
LocalFrame swivelAboutModelZ(const LocalFrame& point, double turn);

struct LocalTriangle {
    float p[3][3];  // model space, metres, Z up
    Rgb colour;     // flat colour, used when there is no texture
    int texture = -1;  // index into PreparedMesh::textures, or -1
    float uv[3][2] = {{0, 0}, {0, 0}, {0, 0}};
};

struct PreparedMesh {
    std::string meshName;  // the file used, inside the asset folder
    std::vector<LocalTriangle> triangles;
    // Small copies of the parts' colour maps (the largest mip no bigger than
    // kTextureSide). Not changed after loading, so Scene may point at them.
    std::vector<SceneTexture> textures;
    bool placeholder = false;  // an invented stand-in shape, not the asset's model
    // The model's attachment points by name, in model space (first part's wins on a repeat).
    std::map<std::string, LocalFrame> attachments;
    // A traincar's bogies, drawn on its "a.bog<N>" points (bare numbers read as user -1; the
    // disc's bogie folders confirm it, e.g. 100008 = "kuid _1 100008"). Two forms on the disc:
    // "bogey <kuid>" (827 traincars, one model for every point) and a "bogeys" group (328
    // traincars, 634 entries) whose entry named N is for a.bog<N> (documented in the Trainz
    // wiki "Bogeys" container page; not traced in the exe). "reversed" reverses the wheel
    // animation (documented); the bogie is turned only by its point's rotation. On the disc
    // 64 of the 71 reversed=1 points are already turned 180 degrees, as the wiki describes.
    std::optional<trainz::content::Kuid> bogey;
    struct BogeyEntry {
        trainz::content::Kuid kuid;
        bool reversed = false;
    };
    std::map<int, BogeyEntry> bogeys;
    // The bogie for one attachment point name, if it is an "a.bog<N>" point with one.
    std::optional<BogeyEntry> bogeyFor(const std::string& pointName) const;
};

inline constexpr std::uint32_t kTextureSide = 128;

// The stand-in for a SpeedTree tree (.srt is not read): a green 8-sided cone, 4 m wide and
// 12 m tall, starting 1.5 m up. An invented shape, clearly not the real tree.
PreparedMesh placeholderTree();

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
    // A reskin's own .texture files, by lowercased file name (no folder): drawn through its
    // base model, a base texture with one of these names comes from the reskin instead
    // (observed, e.g. "main.texture"; not traced).
    struct TextureOverride {
        const trainz::content::JaArchive* archive = nullptr;
        std::map<std::string, std::string> byName;  // "main.texture" -> full entry name
    };
    std::unique_ptr<PreparedMesh> load(const trainz::content::Kuid& kuid, const TextureOverride* reskin = nullptr);
    std::optional<TextureOverride> reskinFiles(const trainz::content::Kuid& reskin);
    std::optional<trainz::content::Kuid> reskinBase(const trainz::content::Kuid& kuid);
    // Adds one mesh file's triangles; false (and counted) if it can't be read.
    // The triangles are placed by `frame`; the mesh's own attachment points, placed the same
    // way, are added to `points`.
    bool appendMesh(const trainz::content::JaArchive& archive, const std::string& base, std::string mesh,
                    PreparedMesh& out, const LocalFrame& frame, std::map<std::string, LocalFrame>& points,
                    const TextureOverride* reskin = nullptr);

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

// One mesh the view draws: its mesh-table entry name ("" for a top-level "mesh" or
// asset-filename), the file (as written, relative to the asset folder) and, for an attached
// part, the point ("att") on the parent entry ("att-parent", "default" when not given).
struct MeshChoice {
    std::string entry, mesh, att, attParent;
    bool operator==(const MeshChoice&) const = default;
};

// Which meshes of an asset config the view draws, with counts of what was skipped and why
// added to `parts`. See the rules in AssetMeshes.cpp; they are the port's reading of the key
// names, not traced.
std::vector<MeshChoice> selectAssetMeshes(const trainz::content::ConfigNode& root,
                                          std::map<std::string, std::size_t>& parts);

// The KUID a config value names: a KUID, a string holding one, or a bare number (user -1).
std::optional<trainz::content::Kuid> configKuid(const trainz::content::ConfigNode& value);

// HEURISTIC (not traced): whether a texture's alpha looks like a cut-out mask (at least
// 5% clear and 1% solid texels, and under half in between), so the view drops the
// clear texels. Other textures are drawn solid, as their alpha may mean something else.
bool wantsAlphaTest(std::span<const std::uint8_t> rgba);

// Places a prepared mesh in the world: rotate by yaw (radians, anticlockwise, about +z)
// and move to (x, y, z). Appends to the scene; the mesh's textures are added to
// scene.textures once per mesh (by pointer), so the mesh must outlive the render.
void addPlacedMesh(Scene& scene, const PreparedMesh& mesh, double x, double y, double z, double yaw);
// The same, with the mesh first placed by `inner` inside the model (e.g. a bogie on its point).
void addPlacedMesh(Scene& scene, const PreparedMesh& mesh, const LocalFrame& inner, double x, double y, double z,
                   double yaw);

}  // namespace trainz::view
