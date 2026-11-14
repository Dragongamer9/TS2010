#include "AssetMeshes.h"

#include "content/ArchivePath.h"
#include "content/AssetConfig.h"
#include "content/ContentError.h"
#include "content/MeshFile.h"
#include "content/TextureFile.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cstring>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <span>

namespace trainz::view {

using namespace trainz::content;

namespace {

constexpr std::size_t kMaxFileBytes = std::size_t{64} << 20;

std::string folderOf(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// A .texture as a SceneTexture: the largest mip no bigger than `side` plus the
// smaller ones, alphaTest by wantsAlphaTest. nullopt if it has no usable mip. Throws
// ContentError for damaged files.
std::optional<SceneTexture> loadSceneTexture(const JaArchive& a, const JaEntry& e, std::uint32_t side = kTextureSide) {
    const TextureFile tex = parseTexture(a.readLimited(e, kMaxFileBytes));
    if (tex.mips.empty()) {
        return std::nullopt;
    }
    // mips are largest first: take the first that fits kTextureSide.
    const MipLevel* pick = &tex.mips.back();
    for (const auto& mip : tex.mips) {
        if (mip.width <= side && mip.height <= side) {
            pick = &mip;
            break;
        }
    }
    SceneTexture st;
    st.width = pick->width, st.height = pick->height;
    st.rgba = decodeToRgba8(*pick, tex.info.format);
    st.alphaTest = wantsAlphaTest(st.rgba);
    for (const MipLevel* lower = pick + 1; lower < tex.mips.data() + tex.mips.size(); ++lower) {
        SceneTexture smaller;  // the file's own smaller mips, for distant surfaces
        smaller.width = lower->width, smaller.height = lower->height;
        smaller.rgba = decodeToRgba8(*lower, tex.info.format);
        st.smaller.push_back(std::move(smaller));
    }
    if (st.width == 0 || st.height == 0 || st.rgba.size() != std::size_t{st.width} * st.height * 4) {
        return std::nullopt;
    }
    return st;
}

}  // namespace

std::optional<SpeedTreeBox> readSpeedTreeBox(std::span<const std::uint8_t> srt) {
    static constexpr char kMagic[] = "SRT 05.0.1";
    if (srt.size() < 60 || std::memcmp(srt.data(), kMagic, sizeof kMagic - 1) != 0) {
        return std::nullopt;
    }
    SpeedTreeBox b;
    std::memcpy(b.min, srt.data() + 36, 12);
    std::memcpy(b.max, srt.data() + 48, 12);
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(b.min[i]) || !std::isfinite(b.max[i]) || !(b.max[i] > b.min[i]) || std::fabs(b.min[i]) > 1e4F ||
            std::fabs(b.max[i]) > 1e4F) {
            return std::nullopt;
        }
    }
    return b;
}

std::optional<AtlasCell> firstAtlasCell(const SceneTexture& atlas) {
    const std::uint32_t w = atlas.width, h = atlas.height;
    if (w == 0 || h == 0 || atlas.rgba.size() < std::size_t{w} * h * 4) {
        return std::nullopt;
    }
    auto solid = [&](std::uint32_t x, std::uint32_t y) { return atlas.rgba[(std::size_t{y} * w + x) * 4 + 3] >= 128; };
    auto rowHas = [&](std::uint32_t y, std::uint32_t x0, std::uint32_t x1) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            if (solid(x, y)) {
                return true;
            }
        }
        return false;
    };
    // Runs of rows (then columns) holding solid texels; runs shorter than 1/16 of the side
    // are specks, not a tree (seen on the disc: stray texels above the images).
    const std::uint32_t minH = std::max<std::uint32_t>(2, h / 16), minW = std::max<std::uint32_t>(2, w / 16);
    std::uint32_t y0 = 0, y1 = 0;
    for (std::uint32_t y = 0; y < h;) {
        if (!rowHas(y, 0, w)) {
            ++y;
            continue;
        }
        std::uint32_t e = y;
        while (e < h && rowHas(e, 0, w)) {
            ++e;
        }
        if (e - y >= minH) {
            y0 = y, y1 = e;
            break;
        }
        y = e;
    }
    if (y1 == 0) {
        return std::nullopt;  // nothing big enough
    }
    auto colHas = [&](std::uint32_t x) {
        for (std::uint32_t y = y0; y < y1; ++y) {
            if (solid(x, y)) {
                return true;
            }
        }
        return false;
    };
    std::uint32_t x0 = 0, x1 = 0;
    for (std::uint32_t x = 0; x < w;) {
        if (!colHas(x)) {
            ++x;
            continue;
        }
        std::uint32_t e = x;
        while (e < w && colHas(e)) {
            ++e;
        }
        if (e - x >= minW) {
            x0 = x, x1 = e;
            break;
        }
        x = e;
    }
    if (x1 == 0) {
        return std::nullopt;
    }
    // Rows of this column run only (the row band may span several images).
    std::uint32_t top = y0, bottom = y1;
    while (top < bottom && !rowHas(top, x0, x1)) {
        ++top;
    }
    while (bottom > top && !rowHas(bottom - 1, x0, x1)) {
        --bottom;
    }
    return AtlasCell{x0, top, x1, bottom};
}

PreparedMesh speedTreeBillboard(const SpeedTreeBox& box, SceneTexture atlas, const AtlasCell& cell, double scale) {
    PreparedMesh m;
    m.billboard = true;
    if (!(scale > 0) || !std::isfinite(scale) || cell.x1 <= cell.x0 || cell.y1 <= cell.y0 || atlas.width == 0 || atlas.height == 0) {
        return m;
    }
    atlas.alphaTest = true;  // the billboard is a cut-out
    const float s = static_cast<float>(scale);
    const float z0 = box.min[2] * s, z1 = box.max[2] * s;
    const float aspect = static_cast<float>(cell.x1 - cell.x0) / static_cast<float>(cell.y1 - cell.y0);
    const float half = 0.5F * aspect * (z1 - z0);
    const float cx = 0.5F * (box.min[0] + box.max[0]) * s, cy = 0.5F * (box.min[1] + box.max[1]) * s;
    const float u0 = static_cast<float>(cell.x0) / static_cast<float>(atlas.width);
    const float u1 = static_cast<float>(cell.x1) / static_cast<float>(atlas.width);
    const float vTop = static_cast<float>(cell.y0) / static_cast<float>(atlas.height);
    const float vBottom = static_cast<float>(cell.y1) / static_cast<float>(atlas.height);
    m.textures.push_back(std::move(atlas));
    // Two upright quads: along x, and along y.
    for (int q = 0; q < 2; ++q) {
        const float dx = q == 0 ? half : 0, dy = q == 0 ? 0 : half;
        const float p[4][3] = {{cx - dx, cy - dy, z0}, {cx + dx, cy + dy, z0}, {cx + dx, cy + dy, z1}, {cx - dx, cy - dy, z1}};
        const float uv[4][2] = {{u0, vBottom}, {u1, vBottom}, {u1, vTop}, {u0, vTop}};
        for (const auto& tri : {std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3}}) {
            LocalTriangle t{};
            for (int k = 0; k < 3; ++k) {
                for (int i = 0; i < 3; ++i) {
                    t.p[k][i] = p[tri[static_cast<std::size_t>(k)]][i];
                }
                t.uv[k][0] = uv[tri[static_cast<std::size_t>(k)]][0];
                t.uv[k][1] = uv[tri[static_cast<std::size_t>(k)]][1];
            }
            t.colour = {60, 110, 60};
            t.texture = 0;
            m.triangles.push_back(t);
        }
    }
    return m;
}

PreparedMesh placeholderTree() {
    PreparedMesh m;
    m.meshName = "(placeholder for a SpeedTree tree)";
    m.placeholder = true;
    constexpr int kSides = 8;
    constexpr float kRadius = 2.0F, kHeight = 12.0F, kTrunk = 1.5F;
    for (int i = 0; i < kSides; ++i) {
        const float a0 = static_cast<float>(i) * 2.0F * std::numbers::pi_v<float> / kSides;
        const float a1 = static_cast<float>(i + 1) * 2.0F * std::numbers::pi_v<float> / kSides;
        LocalTriangle t{};
        t.p[0][0] = kRadius * std::cos(a0), t.p[0][1] = kRadius * std::sin(a0), t.p[0][2] = kTrunk;
        t.p[1][0] = kRadius * std::cos(a1), t.p[1][1] = kRadius * std::sin(a1), t.p[1][2] = kTrunk;
        t.p[2][2] = kHeight;
        t.colour = {40, 95, 45};
        m.triangles.push_back(t);
    }
    return m;
}

std::string highestDetailLodMesh(std::span<const std::uint8_t> text) {
    // Lines such as:  mesh("1.0")  {  name="body.im";  }   (observed in .lm.txt files)
    const std::string t(reinterpret_cast<const char*>(text.data()), text.size());
    std::string best;
    double bestLevel = -1;
    std::size_t pos = 0;
    while ((pos = t.find("mesh(", pos)) != std::string::npos) {
        const std::size_t q1 = t.find('"', pos), q2 = q1 == std::string::npos ? q1 : t.find('"', q1 + 1);
        const std::size_t n = t.find("name", pos);
        if (q2 == std::string::npos || n == std::string::npos) {
            break;
        }
        const std::size_t n1 = t.find('"', n), n2 = n1 == std::string::npos ? n1 : t.find('"', n1 + 1);
        if (n2 == std::string::npos) {
            break;
        }
        double level = -1;
        try {
            level = std::stod(t.substr(q1 + 1, q2 - q1 - 1));
        } catch (const std::exception&) {
        }
        if (level > bestLevel) {
            bestLevel = level;
            best = t.substr(n1 + 1, n2 - n1 - 1);
        }
        pos = n2;
    }
    for (char& ch : best) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    return best;
}

// The meshes to draw from a config (the port's reading of the key names; the game's own
// rules are not traced):
// - "mesh" at the top level; or
// - every mesh-table entry that is the full-detail, daytime model at the asset's origin.
//   Skipped and counted: "lod-level" above 0 (simpler copies for distance),
//   "night-mesh-base" (night variants), "att" (hangs from an attachment point, not
//   decoded yet) and shadow meshes (by name; flat dark planes for the shadow effect);
// - with neither, "asset-filename" + ".im" (an older convention; observed, not traced).
std::optional<Kuid> configKuid(const ConfigNode& value) {
    if (auto k = value.asKuid()) {
        return k;
    }
    if (value.type == ConfigType::Integers && value.ints.size() == 1 && value.ints[0] >= 0) {
        return Kuid{-1, value.ints[0], std::nullopt};  // a bare number, as for "kuid 101135"
    }
    return std::nullopt;
}

std::vector<MeshChoice> selectAssetMeshes(const ConfigNode& root, std::map<std::string, std::size_t>& parts) {
    std::vector<MeshChoice> out;
    if (const auto* m = root.child("mesh"); m != nullptr && m->type == ConfigType::String) {
        out.push_back({"", m->text, "", "", 1.0});
        return out;
    }
    if (const auto* table = root.child("mesh-table"); table != nullptr) {
        for (const auto& group : table->children) {
            const auto* m = group.child("mesh");
            if (m == nullptr || m->type != ConfigType::String) {
                continue;
            }
            if (const auto* lod = group.child("lod-level");
                lod != nullptr && lod->type == ConfigType::Integers && !lod->ints.empty() && lod->ints[0] > 0) {
                ++parts["mesh-table entries skipped: lower level of detail"];
                continue;
            }
            if (group.child("night-mesh-base") != nullptr) {
                ++parts["mesh-table entries skipped: night variant"];
                continue;
            }
            if (asciiLower(m->text).find("shadow") != std::string::npos) {
                ++parts["mesh-table entries skipped: shadow mesh"];
                continue;
            }
            MeshChoice c{group.key, m->text, "", "", 1.0};
            if (const auto* sc = group.child("mesh-scale"); sc != nullptr && sc->type == ConfigType::Floats && !sc->floats.empty()) {
                double sum = 0;
                for (const float f : sc->floats) {
                    sum += f;
                }
                const double mean = sum / static_cast<double>(sc->floats.size());
                if (std::isfinite(mean) && mean > 0 && mean < 1000) {
                    c.scale = mean;  // two values: a range (inferred); the mean is used
                }
            }
            if (const auto* att = group.child("att"); att != nullptr) {
                if (att->type != ConfigType::String || att->text.empty()) {
                    ++parts["mesh-table entries skipped: unreadable att"];
                    continue;
                }
                c.att = att->text;
                const auto* parent = group.child("att-parent");
                c.attParent = parent != nullptr && parent->type == ConfigType::String && !parent->text.empty()
                                  ? parent->text
                                  : "default";  // the port's reading when none is given (837 disc entries)
            }
            out.push_back(std::move(c));
        }
        return out;
    }
    if (const auto* f = root.child("asset-filename"); f != nullptr && f->type == ConfigType::String && !f->text.empty()) {
        out.push_back({"", f->text + ".im", "", "", 1.0});
        ++parts["models found by asset-filename"];
    }
    return out;
}


const JaArchive* AssetMeshes::archive(const std::filesystem::path& path) {
    auto& slot = archives_[path.string()];
    if (!slot) {
        slot = std::make_unique<JaArchive>(JaArchive::open(path));
    }
    return slot.get();
}

std::optional<std::vector<std::uint8_t>> AssetMeshes::file(const Kuid& kuid, const std::string& name, std::size_t limit) {
    auto records = index_.find(kuid);
    if (records.empty()) {
        records = index_.allVersions(kuid.user, kuid.content);
        if (!records.empty()) {
            records.assign(1, records.back());
        }
    }
    if (records.empty() || records.front()->where.container != AssetLocation::Container::Archive) {
        return std::nullopt;
    }
    const AssetRecord& rec = *records.front();
    try {
        const JaArchive* a = archive(rec.where.source);
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string base = rec.where.folder.empty() ? std::string() : rec.where.folder + "/";
        const JaEntry* e = a->find(base + lower);
        if (e == nullptr) {
            return std::nullopt;
        }
        return a->readLimited(*e, limit);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<ConfigNode> AssetMeshes::config(const Kuid& kuid) {
    auto records = index_.find(kuid);
    if (records.empty()) {
        records = index_.allVersions(kuid.user, kuid.content);
        if (!records.empty()) {
            records.assign(1, records.back());
        }
    }
    if (records.empty() || records.front()->where.container != AssetLocation::Container::Archive) {
        return std::nullopt;
    }
    const AssetRecord& rec = *records.front();
    try {
        const JaArchive* a = archive(rec.where.source);
        const std::string base = rec.where.folder.empty() ? std::string() : rec.where.folder + "/";
        const JaEntry* ce = a->find(base + rec.where.configName);
        if (ce == nullptr) {
            return std::nullopt;
        }
        return parseAssetConfig(a->readLimited(*ce, std::size_t{16} << 20)).root;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::unique_ptr<PreparedMesh> AssetMeshes::load(const Kuid& kuid, const TextureOverride* reskin) {
    auto records = index_.find(kuid);
    if (records.empty()) {
        // Another version of the same asset (newest last), as installs often differ.
        records = index_.allVersions(kuid.user, kuid.content);
        if (!records.empty()) {
            const AssetRecord* newest = records.back();
            records.assign(1, newest);
        }
    }
    if (records.empty()) {
        ++misses_["asset not in the index"];
        return nullptr;
    }
    const AssetRecord& rec = *records.front();  // which copy the game prefers is not traced
    if (rec.where.container != AssetLocation::Container::Archive) {
        ++misses_["asset in a folder (not supported here yet)"];
        return nullptr;
    }
    const JaArchive* a = archive(rec.where.source);
    const std::string base = rec.where.folder.empty() ? std::string() : rec.where.folder + "/";
    const JaEntry* ce = a->find(base + rec.where.configName);
    if (ce == nullptr) {
        ++misses_["config missing"];
        return nullptr;
    }
    std::vector<MeshChoice> meshes;
    std::optional<Kuid> bogey;
    std::map<int, PreparedMesh::BogeyEntry> bogeys;
    bool surveyorOnly = false;
    try {
        const ConfigNode root = parseAssetConfig(a->readLimited(*ce, std::size_t{16} << 20)).root;
        meshes = selectAssetMeshes(root, parts_);
        if (const auto* so = root.child("surveyor-only")) {
            surveyorOnly = (!so->ints.empty() && so->ints[0] != 0) || (!so->floats.empty() && so->floats[0] != 0) ||
                           so->text == "1" || so->text == "true";
        }
        if (const auto* b = root.child("bogey")) {
            bogey = configKuid(*b);
        }
        if (const auto* group = root.child("bogeys"); group != nullptr && group->type == ConfigType::Group) {
            for (const auto& e : group->children) {
                const auto* k = e.child("bogey");
                const auto named = k != nullptr ? configKuid(*k) : std::nullopt;
                if (!named || e.key.empty() || e.key.size() > 3 || e.key.find_first_not_of("0123456789") != std::string::npos) {
                    ++parts_["bogeys entries not understood"];
                    continue;
                }
                const auto* rev = e.child("reversed");
                const bool reversed = rev != nullptr && rev->type == ConfigType::Integers && !rev->ints.empty() && rev->ints[0] != 0;
                bogeys.emplace(std::stoi(e.key), PreparedMesh::BogeyEntry{*named, reversed});
            }
        }
    } catch (const ContentError&) {
        ++misses_["config unreadable"];
        return nullptr;
    }
    if (meshes.empty()) {
        ++misses_["no mesh in the config"];
        return nullptr;
    }
    auto out = std::make_unique<PreparedMesh>();
    out->surveyorOnly = surveyorOnly;
    out->bogey = bogey;
    out->bogeys = std::move(bogeys);
    bool speedTree = false;
    bool billboardDone = false;
    bool overLimit = false;
    // Each drawn entry's attachment points (model space), for the parts hung on them.
    std::map<std::string, std::map<std::string, LocalFrame>> pointsOf;
    auto draw = [&](const MeshChoice& c, const LocalFrame& frame) {
        if (asciiLower(c.mesh).ends_with(".srt")) {
            // SpeedTree trees ("SRT " files): their geometry is not read; drawn as a billboard
            // of their own atlas image when possible. Seasons are separate entries: only the
            // first tree entry is drawn.
            speedTree = true;
            if (!billboardDone && c.att.empty()) {
                if (auto bb = loadSpeedTreeBillboard(*a, base, c)) {
                    const int offset = static_cast<int>(out->textures.size());
                    for (auto& t : bb->triangles) {
                        t.texture = t.texture >= 0 ? t.texture + offset : -1;
                        out->triangles.push_back(t);
                    }
                    for (auto& t : bb->textures) {
                        out->textures.push_back(std::move(t));
                    }
                    out->meshName += (out->meshName.empty() ? "" : ", ") + bb->meshName;
                    out->billboard = true;
                    billboardDone = true;
                    ++parts_["SpeedTree trees drawn as billboards of their own atlas image"];
                    return;
                }
            }
            ++parts_["mesh-table entries skipped: SpeedTree tree (.srt, not read)"];
            return;
        }
        std::map<std::string, LocalFrame> points;
        if (!appendMesh(*a, base, c.mesh, *out, frame, points, reskin)) {
            overLimit = overLimit || out->triangles.size() >= kMaxTrianglesPerMesh;
            return;  // that part is counted in parts_; the others may still draw
        }
        ++parts_[c.att.empty() ? "mesh-table entries drawn" : "mesh-table entries drawn on an attachment point"];
        if (c.att.empty()) {
            out->attachments.insert(points.begin(), points.end());  // the first part's point wins
        }
        pointsOf.emplace(c.entry, std::move(points));
    };
    std::vector<const MeshChoice*> attached;
    for (const auto& c : meshes) {
        if (c.att.empty()) {
            draw(c, LocalFrame{});
        } else {
            attached.push_back(&c);
        }
    }
    // Attached parts once their parent is drawn (parents can be attached parts themselves).
    for (bool progress = true; progress && !attached.empty() && !overLimit;) {
        progress = false;
        for (auto it = attached.begin(); it != attached.end();) {
            const MeshChoice& c = **it;
            const auto parent = std::find_if(pointsOf.begin(), pointsOf.end(),
                                             [&](const auto& kv) { return asciiLower(kv.first) == asciiLower(c.attParent); });
            if (parent == pointsOf.end()) {
                ++it;
                continue;
            }
            const auto point = std::find_if(parent->second.begin(), parent->second.end(),
                                            [&](const auto& kv) { return asciiLower(kv.first) == asciiLower(c.att); });
            if (point == parent->second.end()) {
                ++parts_["mesh-table entries skipped: attachment point not in the parent mesh"];
            } else {
                draw(c, point->second);
            }
            it = attached.erase(it);
            progress = true;
        }
    }
    if (overLimit) {
        ++misses_["over the triangle limit"];
        return nullptr;
    }
    parts_["mesh-table entries skipped: parent part not drawn"] += attached.size();
    if (out->triangles.empty() && speedTree) {
        // A PLACEHOLDER, not the tree: a green cone, 4 m wide and 12 m tall.
        *out = placeholderTree();
        ++parts_["SpeedTree trees drawn as PLACEHOLDER cones (not their real shape)"];
        return out;
    }
    if (out->triangles.empty()) {
        ++misses_["no readable mesh"];
        return nullptr;
    }
    return out;
}

std::optional<PreparedMesh> AssetMeshes::loadSpeedTreeBillboard(const JaArchive& a, const std::string& base,
                                                               const MeshChoice& c) {
    std::string path = base + c.mesh;
    std::replace(path.begin(), path.end(), '\\', '/');
    auto fail = [&](const char* why) -> std::optional<PreparedMesh> {
        ++parts_[std::string("SpeedTree billboard not made: ") + why];
        return std::nullopt;
    };
    const JaEntry* se = a.find(path);
    if (se == nullptr) {
        return fail(".srt missing");
    }
    const auto box = readSpeedTreeBox(a.readLimited(*se, kMaxFileBytes));
    if (!box) {
        return fail("header not understood");
    }
    // The atlas beside the .srt (all 379 disc trees have one; observed name).
    const JaEntry* ae = a.find(folderOf(path) + "billboardatlas_diffuse.texture");
    if (ae == nullptr) {
        return fail("no billboardatlas_diffuse.texture beside it");
    }
    auto atlas = loadSceneTexture(a, *ae, kAtlasSide);  // several views share it: more texels
    if (!atlas) {
        return fail("atlas unreadable");
    }
    const auto cell = firstAtlasCell(*atlas);
    if (!cell) {
        return fail("no tree image found in the atlas");
    }
    PreparedMesh m = speedTreeBillboard(*box, std::move(*atlas), *cell, c.scale);
    m.meshName = path + " (billboard)";
    return m;
}

bool AssetMeshes::appendMesh(const JaArchive& archiveRef, const std::string& base, std::string mesh, PreparedMesh& out,
                             const LocalFrame& frame, std::map<std::string, LocalFrame>& points,
                             const TextureOverride* reskin) {
    const JaArchive* a = &archiveRef;
    for (char& c : mesh) {
        if (c == '\\') {
            c = '/';
        }
    }
    std::string meshPath = base + mesh;
    if (asciiLower(meshPath).ends_with(".lm")) {
        // No .lm file exists on the disc; the archives ship "<name>.lm.txt" text instead (796
        // of them). Use its highest-detail mesh, else an .im of the same name.
        std::string chosen;
        if (const JaEntry* lt = a->find(meshPath + ".txt")) {
            try {
                chosen = highestDetailLodMesh(a->readLimited(*lt, std::size_t{1} << 20));
            } catch (const ContentError&) {
            }
        }
        if (!chosen.empty()) {
            meshPath = folderOf(meshPath) + chosen;
            ++parts_["models found through a .lm.txt"];
        } else if (const std::string im = meshPath.substr(0, meshPath.size() - 3) + ".im"; a->find(im) != nullptr) {
            meshPath = im;
        } else {
            ++parts_["mesh-table entries skipped: .lm with no readable .lm.txt or .im"];
            return false;
        }
    }
    const JaEntry* me = a->find(meshPath);
    if (me == nullptr && asciiLower(meshPath).ends_with(".im")) {
        // Observed: 286 disc assets name an asset-filename that exists only as a .pm
        // (progressive mesh, same chunks), e.g. bogies.
        me = a->find(meshPath.substr(0, meshPath.size() - 3) + ".pm");
        if (me != nullptr) {
            meshPath = me->name;
            ++parts_["models found as .pm instead of .im"];
        }
    }
    if (me == nullptr) {
        ++parts_["mesh-table entries skipped: mesh file missing"];
        return false;
    }
    MeshFile m;
    try {
        m = parseMeshOrRepair(a->readLimited(*me, kMaxFileBytes));
    } catch (const ContentError&) {
        ++parts_["mesh-table entries skipped: mesh unreadable"];
        return false;
    }
    out.meshName += (out.meshName.empty() ? "" : ", ") + meshPath;
    for (const auto& at : m.attachments) {
        points.emplace(at.name, LocalFrame::of(at).then(frame));
    }
    const std::string meshDir = folderOf(meshPath);
    std::map<std::string, int> textureByFile;  // one copy per file within this asset
    for (const auto& part : m.parts) {
        Rgb colour{170, 165, 155};
        int texture = -1;
        for (const auto& t : part.material.textures) {
            if (t.type != 1 || t.fileName.empty()) {
                continue;  // only the colour map
            }
            std::string file = meshDir + t.fileName;
            const JaArchive* from = a;
            // A Paint Shed reskin drawn through its base model: a texture whose file name the
            // reskin also has comes from the reskin (observed: e.g. its "main.texture"; not traced).
            if (reskin != nullptr && reskin->archive != nullptr) {
                const std::string leaf = asciiLower(t.fileName.substr(t.fileName.find_last_of("/\\") + 1));
                if (const auto it = reskin->byName.find(leaf); it != reskin->byName.end()) {
                    file = it->second, from = reskin->archive;
                    ++parts_["textures taken from the reskin"];
                }
            }
            if (const auto known = textureByFile.find(file); known != textureByFile.end()) {
                texture = known->second;
                break;
            }
            const JaEntry* te = from->find(file);
            if (te == nullptr) {
                break;
            }
            try {
                if (auto st = loadSceneTexture(*from, *te)) {
                    if (st->alphaTest && !materialMayCutOut(part.material.kind)) {
                        st->alphaTest = false;  // its alpha is a reflection mask, not holes
                        ++parts_["cut-out alpha ignored for gloss materials"];
                    }
                    texture = static_cast<int>(out.textures.size());
                    out.textures.push_back(std::move(*st));
                    textureByFile.emplace(file, texture);
                }
            } catch (const ContentError&) {
                // keep the flat colour
            }
            break;
        }
        const auto& g = part.geometry;
        for (std::size_t i = 0; i + 2 < g.indices.size(); i += 3) {
            if (out.triangles.size() >= kMaxTrianglesPerMesh) {
                return false;
            }
            LocalTriangle tri{};
            bool ok = true;
            for (int k = 0; k < 3; ++k) {
                const std::size_t vi = g.indices[i + static_cast<std::size_t>(k)];
                if (vi >= g.vertices.size()) {
                    ok = false;
                    break;
                }
                const float local[3] = {g.vertices[vi].x, g.vertices[vi].y, g.vertices[vi].z};
                frame.apply(local, tri.p[k]);
                tri.uv[k][0] = g.vertices[vi].u, tri.uv[k][1] = g.vertices[vi].v;
            }
            if (ok) {
                tri.colour = colour;
                tri.texture = texture;
                out.triangles.push_back(tri);
            }
        }
    }
    return true;
}

std::optional<AssetMeshes::TextureOverride> AssetMeshes::reskinFiles(const Kuid& reskin) {
    auto records = index_.find(reskin);
    if (records.empty() || records.front()->where.container != AssetLocation::Container::Archive) {
        return std::nullopt;
    }
    const AssetRecord& rec = *records.front();
    TextureOverride o;
    o.archive = archive(rec.where.source);
    const std::string prefix = asciiLower(rec.where.folder.empty() ? std::string() : rec.where.folder + "/");
    for (const auto& e : o.archive->entries()) {  // names are stored lowercased
        if (e.name.size() > prefix.size() && e.name.compare(0, prefix.size(), prefix) == 0 && e.name.ends_with(".texture")) {
            const std::string leaf = e.name.substr(e.name.find_last_of('/') + 1);
            o.byName.emplace(leaf, e.name);  // the first (shallowest in index order) wins
        }
    }
    return o;
}

std::optional<Kuid> AssetMeshes::reskinBase(const Kuid& kuid) {
    // A Paint Shed reskin's skin.dat starts "1.6", then "<KUID:template>\<KUID:base>"; the
    // base (2nd KUID) holds the meshes the reskin names. Observed: for 25 of the 61 disc
    // reskins with two KUIDs the base has a model the port reads; not traced.
    auto records = index_.find(kuid);
    if (records.empty() || records.front()->where.container != AssetLocation::Container::Archive) {
        return std::nullopt;
    }
    const AssetRecord& rec = *records.front();
    const JaArchive* a = archive(rec.where.source);
    const JaEntry* sk = a->find((rec.where.folder.empty() ? std::string() : rec.where.folder + "/") + "skin.dat");
    if (sk == nullptr) {
        return std::nullopt;
    }
    const auto bytes = a->readLimited(*sk, std::size_t{64} << 10);
    const std::string text(bytes.begin(), bytes.end());
    std::size_t pos = 0;
    int found = 0;
    while ((pos = asciiLower(text).find("<kuid", pos)) != std::string::npos) {
        const std::size_t end = text.find('>', pos);
        if (end == std::string::npos) {
            break;
        }
        if (++found == 2) {
            return parseKuidLoose(text.substr(pos, end - pos + 1));
        }
        pos = end;
    }
    return std::nullopt;
}

const PreparedMesh* AssetMeshes::get(const Kuid& kuid) {
    const std::string key = formatKuid(kuid);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        std::unique_ptr<PreparedMesh> loaded;
        try {
            loaded = load(kuid);
            if (!loaded) {
                if (const auto base = reskinBase(kuid)) {
                    const auto own = reskinFiles(kuid);
                    loaded = load(*base, own ? &*own : nullptr);  // a Paint Shed reskin without a mesh of its own
                    if (loaded) {
                        ++parts_["models taken from a reskin's base asset"];
                    }
                }
            }
        } catch (const ContentError&) {
            ++misses_["archive unreadable"];
        }
        it = cache_.emplace(key, std::move(loaded)).first;
    }
    return it->second.get();
}

bool materialMayCutOut(const std::string& kind) {
    const std::string k = asciiLower(kind);
    return k != "m.gloss" && k != "m.tbumpgloss";
}

bool wantsAlphaTest(std::span<const std::uint8_t> rgba) {
    std::size_t clear = 0, solid = 0;
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
        (rgba[i] < 128 ? clear : solid) += 1;
    }
    const std::size_t n = clear + solid;
    // HEURISTIC: a real mix of see-through and solid texels (at least 5% clear, 1% solid,
    // and under half in between) suggests a cut-out map (leaves, grass, fences). Not traced:
    // the game's material flags for this are not decoded. Checked by listing what the rule
    // changes in two views: grass, flowers, trees, lichen, a chain and a window alpha.
    if (n == 0 || clear * 20 < n || solid * 100 < n) {
        return false;
    }
    std::size_t middle = 0;
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
        middle += rgba[i] > 32 && rgba[i] < 224;
    }
    return middle * 2 < n;
}

std::optional<PreparedMesh::BogeyEntry> PreparedMesh::bogeyFor(const std::string& pointName) const {
    // "a.bog<N>", any case
    if (pointName.size() < 6 || asciiLower(pointName.substr(0, 5)) != "a.bog") {
        return std::nullopt;
    }
    const std::string digits = pointName.substr(5);
    if (digits.size() > 3 || digits.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }
    if (const auto it = bogeys.find(std::stoi(digits)); it != bogeys.end()) {
        return it->second;
    }
    if (bogey) {
        return BogeyEntry{*bogey, false};
    }
    return std::nullopt;
}

LocalFrame LocalFrame::of(const MeshAttachment& a) {
    LocalFrame f;
    std::copy(std::begin(a.rotation), std::end(a.rotation), f.r);
    std::copy(std::begin(a.position), std::end(a.position), f.t);
    return f;
}

void LocalFrame::apply(const float in[3], float out[3]) const {
    const float x = in[0], y = in[1], z = in[2];  // `out` may be `in`
    for (int i = 0; i < 3; ++i) {
        out[i] = x * r[i] + y * r[3 + i] + z * r[6 + i] + t[i];
    }
}

LocalFrame LocalFrame::then(const LocalFrame& outer) const {
    LocalFrame f;
    for (int axis = 0; axis < 3; ++axis) {  // each of this frame's axes, turned by outer
        for (int i = 0; i < 3; ++i) {
            f.r[3 * axis + i] = r[3 * axis] * outer.r[i] + r[3 * axis + 1] * outer.r[3 + i] + r[3 * axis + 2] * outer.r[6 + i];
        }
    }
    outer.apply(t, f.t);
    return f;
}

LocalFrame swivelAboutModelZ(const LocalFrame& point, double turn) {
    LocalFrame spin;  // about z through point.t: p -> R(p - t) + t
    const float c = static_cast<float>(std::cos(turn)), s = static_cast<float>(std::sin(turn));
    spin.r[0] = c, spin.r[1] = s, spin.r[3] = -s, spin.r[4] = c;
    spin.t[0] = point.t[0] - (c * point.t[0] - s * point.t[1]);
    spin.t[1] = point.t[1] - (s * point.t[0] + c * point.t[1]);
    return point.then(spin);
}

void addPlacedMesh(Scene& scene, const PreparedMesh& mesh, double x, double y, double z, double yaw) {
    addPlacedMesh(scene, mesh, LocalFrame{}, x, y, z, yaw);
}

void addPlacedMesh(Scene& scene, const PreparedMesh& mesh, const LocalFrame& inner, double x, double y, double z,
                   double yaw) {
    const double c = std::cos(yaw), s = std::sin(yaw);
    int base = -1;
    if (!mesh.textures.empty()) {
        for (std::size_t i = 0; i < scene.textures.size(); ++i) {
            if (scene.textures[i] == &mesh.textures.front()) {
                base = static_cast<int>(i);  // already added by an earlier placement
                break;
            }
        }
        if (base < 0) {
            base = static_cast<int>(scene.textures.size());
            for (const auto& t : mesh.textures) {
                scene.textures.push_back(&t);
            }
        }
    }
    for (const auto& t : mesh.triangles) {
        SceneTriangle st;
        const bool textured = t.texture >= 0 && static_cast<std::size_t>(t.texture) < mesh.textures.size();
        if (textured) {
            st.texture = base + t.texture;
        }
        for (int k = 0; k < 3; ++k) {
            float q[3];
            inner.apply(t.p[k], q);
            const double lx = q[0], ly = q[1];
            st.p[k] = {x + c * lx - s * ly, y + s * lx + c * ly, z + q[2]};
            st.c[k] = textured ? Rgb{255, 255, 255} : t.colour;
            st.u[k] = t.uv[k][0], st.v[k] = t.uv[k][1];
        }
        scene.triangles.push_back(st);
    }
}

}  // namespace trainz::view
