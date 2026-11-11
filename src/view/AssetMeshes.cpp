#include "AssetMeshes.h"

#include "content/ArchivePath.h"
#include "content/AssetConfig.h"
#include "content/ContentError.h"
#include "content/MeshFile.h"
#include "content/TextureFile.h"

#include <algorithm>
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

}  // namespace

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
        out.push_back({"", m->text, "", ""});
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
            MeshChoice c{group.key, m->text, "", ""};
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
        out.push_back({"", f->text + ".im", "", ""});
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

std::unique_ptr<PreparedMesh> AssetMeshes::load(const Kuid& kuid) {
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
    try {
        const ConfigNode root = parseAssetConfig(a->readLimited(*ce, std::size_t{16} << 20)).root;
        meshes = selectAssetMeshes(root, parts_);
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
    out->bogey = bogey;
    out->bogeys = std::move(bogeys);
    bool speedTree = false;
    bool overLimit = false;
    // Each drawn entry's attachment points (model space), for the parts hung on them.
    std::map<std::string, std::map<std::string, LocalFrame>> pointsOf;
    auto draw = [&](const MeshChoice& c, const LocalFrame& frame) {
        if (asciiLower(c.mesh).ends_with(".srt")) {
            // SpeedTree trees ("SRT " files) are not read; seasons are separate entries.
            ++parts_["mesh-table entries skipped: SpeedTree tree (.srt, not read)"];
            speedTree = true;
            return;
        }
        std::map<std::string, LocalFrame> points;
        if (!appendMesh(*a, base, c.mesh, *out, frame, points)) {
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

bool AssetMeshes::appendMesh(const JaArchive& archiveRef, const std::string& base, std::string mesh, PreparedMesh& out,
                             const LocalFrame& frame, std::map<std::string, LocalFrame>& points) {
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
            const std::string file = meshDir + t.fileName;
            if (const auto known = textureByFile.find(file); known != textureByFile.end()) {
                texture = known->second;
                break;
            }
            const JaEntry* te = a->find(file);
            if (te == nullptr) {
                break;
            }
            try {
                const TextureFile tex = parseTexture(a->readLimited(*te, kMaxFileBytes));
                if (!tex.mips.empty()) {
                    // mips are largest first: take the first that fits kTextureSide.
                    const MipLevel* pick = &tex.mips.back();
                    for (const auto& mip : tex.mips) {
                        if (mip.width <= kTextureSide && mip.height <= kTextureSide) {
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
                    if (st.width > 0 && st.height > 0 && st.rgba.size() == std::size_t{st.width} * st.height * 4) {
                        texture = static_cast<int>(out.textures.size());
                        out.textures.push_back(std::move(st));
                        textureByFile.emplace(file, texture);
                    }
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
                    loaded = load(*base);  // a Paint Shed reskin without a mesh of its own
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
