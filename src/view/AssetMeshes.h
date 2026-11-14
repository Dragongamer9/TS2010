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
#include <mutex>
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
    bool billboard = false;    // a flat picture of the asset (its own image), not its 3D model
    // The config says "surveyor-only 1": an editor helper the game hides while driving (camera
    // markers, track marks, triggers, direction markers on the disc). Route views skip it.
    bool surveyorOnly = false;
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
inline constexpr std::uint32_t kAtlasSide = 512;  // SpeedTree billboard atlases (one view is a part of it)

// The stand-in for a SpeedTree tree when its billboard can't be made: a green 8-sided cone,
// 4 m wide and 12 m tall, starting 1.5 m up. An invented shape, clearly not the real tree.
PreparedMesh placeholderTree();

// The bounding box in a SpeedTree file's header (OBSERVED, not traced; all 379 disc .srt
// files are "SRT 05.0.1" with 6 floats at byte 36: min x, y, z, max x, y, z). The box's
// proportions match the tree in its billboard images, and multiplied by the config's
// mesh-scale it gives tree heights of 1.5-122 m on the disc (median 13.7 m).
struct SpeedTreeBox {
    float min[3] = {0, 0, 0}, max[3] = {0, 0, 0};
};
std::optional<SpeedTreeBox> readSpeedTreeBox(std::span<const std::uint8_t> srt);

// The first tree image in a billboard atlas (pixels): the first run of columns holding
// solid texels (alpha >= 128) within the first run of such rows, from the top; runs
// shorter than 1/16 of the side are skipped as specks (two disc atlases have stray texels). The atlases
// on the disc hold several views of the tree side by side (e.g. 2 x 3).
struct AtlasCell {
    std::uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // x1/y1 exclusive
};
std::optional<AtlasCell> firstAtlasCell(const SceneTexture& atlas);

// A SpeedTree tree as a BILLBOARD from its own atlas image: two crossed upright quads
// (cut-out) as wide as the image's proportions at the box's height, both scaled by `scale`.
// Not the tree's real 3D shape (the .srt geometry is not read).
PreparedMesh speedTreeBillboard(const SpeedTreeBox& box, SceneTexture atlas, const AtlasCell& cell, double scale);

struct MeshChoice;

class AssetMeshes {
public:
    explicit AssetMeshes(const trainz::content::AssetIndex& index) : index_(index) {}

    // The asset's model, or nullptr if it has none the port can read (no mesh entry, only
    // a .lm with no .im beside it, damaged files, over the triangle limit). Why is recorded.
    const PreparedMesh* get(const trainz::content::Kuid& kuid);

    // The asset's config (the same copy of the asset `get` would draw), or nothing if it is
    // not in the index, not in an archive, or unreadable.
    std::optional<trainz::content::ConfigNode> config(const trainz::content::Kuid& kuid);
    // A file in the asset's folder (name relative to it, compared without case as the archive
    // stores names), at most `limit` bytes; nothing if there is no such file.
    std::optional<std::vector<std::uint8_t>> file(const trainz::content::Kuid& kuid, const std::string& name,
                                                  std::size_t limit = std::size_t{32} << 20);

    // Counts of why assets had no model, for reporting.
    const std::map<std::string, std::size_t>& misses() const { return misses_; }
    // Counts of mesh-table entries drawn and skipped (attachments, shadows, .lm, …).
    const std::map<std::string, std::size_t>& parts() const { return parts_; }

    static constexpr std::size_t kMaxTrianglesPerMesh = 150000;  // the eligible full-detail daytime parts of one asset together

private:
    const trainz::content::JaArchive* archive(const std::filesystem::path& path);
    // A reskin's own .texture files, by lowercased file name (no folder): drawn through its
    // base model, a base texture with one of these names comes from the reskin instead
    // (observed, not traced). Names are matched without case, anywhere in the reskin's
    // folder; on the disc all 28 matches are "main.texture" at the folder's root.
    struct TextureOverride {
        const trainz::content::JaArchive* archive = nullptr;
        std::map<std::string, std::string> byName;  // "main.texture" -> full entry name
    };
    std::unique_ptr<PreparedMesh> load(const trainz::content::Kuid& kuid, const TextureOverride* reskin = nullptr);
    std::optional<TextureOverride> reskinFiles(const trainz::content::Kuid& reskin);
    std::optional<trainz::content::Kuid> reskinBase(const trainz::content::Kuid& kuid);
    std::optional<PreparedMesh> loadSpeedTreeBillboard(const trainz::content::JaArchive& archive, const std::string& base,
                                                       const MeshChoice& choice);
    // Adds one mesh file's triangles; false (and counted) if it can't be read.
    // The triangles are placed by `frame`; the mesh's own attachment points, placed the same
    // way, are added to `points`.
    bool appendMesh(const trainz::content::JaArchive& archive, const std::string& base, std::string mesh,
                    PreparedMesh& out, const LocalFrame& frame, std::map<std::string, LocalFrame>& points,
                    const TextureOverride* reskin = nullptr);

    const trainz::content::AssetIndex& index_;
    // get, config and file may be called from two threads (the viewer builds the next scene in
    // the background): they take this lock. Returned meshes stay put (owned by cache_).
    mutable std::recursive_mutex lock_;
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
    double scale = 1.0;  // mesh-scale (the mean when it gives a range); used for SpeedTree billboards
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

// Whether a material kind's colour texture alpha may be drawn as a cut-out. False for m.gloss
// and m.tbumpgloss: there the diffuse alpha is a reflection strength/mask (DOCUMENTED in the
// Trainz wiki pages "M.gloss" and "M.tbumpgloss", unversioned legacy docs; see
// docs/research/materials.md), and on the disc 277 of 401 sampled m.gloss colour maps have
// smooth alpha. m.onetex, m.tbumptex and m.reflect document diffuse alpha as opacity, so they
// use wantsAlphaTest (all 3,897 m.reflect colour maps on the disc are solid or smooth anyway).
bool materialMayCutOut(const std::string& kind);

// Places a prepared mesh in the world: rotate by yaw (radians, anticlockwise, about +z)
// and move to (x, y, z). Appends to the scene; the mesh's textures are added to
// scene.textures once per mesh (by pointer), so the mesh must outlive the render.
void addPlacedMesh(Scene& scene, const PreparedMesh& mesh, double x, double y, double z, double yaw);
// The same, with the mesh first placed by `inner` inside the model (e.g. a bogie on its point).
void addPlacedMesh(Scene& scene, const PreparedMesh& mesh, const LocalFrame& inner, double x, double y, double z,
                   double yaw);

}  // namespace trainz::view
