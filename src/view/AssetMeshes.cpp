#include "AssetMeshes.h"

#include "content/ArchivePath.h"
#include "content/AssetConfig.h"
#include "content/ContentError.h"
#include "content/MeshFile.h"
#include "content/TextureFile.h"

#include <cmath>
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
std::vector<std::string> selectAssetMeshes(const ConfigNode& root, std::map<std::string, std::size_t>& parts) {
    std::vector<std::string> out;
    if (const auto* m = root.child("mesh"); m != nullptr && m->type == ConfigType::String) {
        out.push_back(m->text);
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
            if (group.child("att") != nullptr) {
                ++parts["mesh-table entries skipped: on an attachment point"];
                continue;
            }
            if (asciiLower(m->text).find("shadow") != std::string::npos) {
                ++parts["mesh-table entries skipped: shadow mesh"];
                continue;
            }
            out.push_back(m->text);
        }
        return out;
    }
    if (const auto* f = root.child("asset-filename"); f != nullptr && f->type == ConfigType::String && !f->text.empty()) {
        out.push_back(f->text + ".im");
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
    std::vector<std::string> meshes;
    try {
        meshes = selectAssetMeshes(parseAssetConfig(a->readLimited(*ce, std::size_t{16} << 20)).root, parts_);
    } catch (const ContentError&) {
        ++misses_["config unreadable"];
        return nullptr;
    }
    if (meshes.empty()) {
        ++misses_["no mesh in the config"];
        return nullptr;
    }
    auto out = std::make_unique<PreparedMesh>();
    for (auto& mesh : meshes) {
        if (!appendMesh(*a, base, mesh, *out)) {
            if (out->triangles.size() >= kMaxTrianglesPerMesh) {
                ++misses_["over the triangle limit"];
                return nullptr;
            }
            continue;  // that part is counted in parts_; the others may still draw
        }
        ++parts_["mesh-table entries drawn"];
    }
    if (out->triangles.empty()) {
        ++misses_["no readable mesh"];
        return nullptr;
    }
    return out;
}

bool AssetMeshes::appendMesh(const JaArchive& archiveRef, const std::string& base, std::string mesh, PreparedMesh& out) {
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
    const std::string meshDir = folderOf(meshPath);
    for (const auto& part : m.parts) {
        Rgb colour{170, 165, 155};
        for (const auto& t : part.material.textures) {
            if (t.type != 1 || t.fileName.empty()) {
                continue;  // only the colour map
            }
            const JaEntry* te = a->find(meshDir + t.fileName);
            if (te == nullptr) {
                break;
            }
            try {
                const TextureFile tex = parseTexture(a->readLimited(*te, kMaxFileBytes));
                if (!tex.mips.empty()) {
                    const auto& mip = tex.mips.back();  // smallest
                    const auto rgba = decodeToRgba8(mip, tex.info.format);
                    double r = 0, g = 0, b = 0;
                    const std::size_t n = rgba.size() / 4;
                    for (std::size_t i = 0; i < n; ++i) {
                        r += rgba[4 * i], g += rgba[4 * i + 1], b += rgba[4 * i + 2];
                    }
                    if (n > 0) {
                        colour = {static_cast<std::uint8_t>(r / n), static_cast<std::uint8_t>(g / n),
                                  static_cast<std::uint8_t>(b / n)};
                    }
                }
            } catch (const ContentError&) {
                // keep the default colour
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
                tri.p[k][0] = g.vertices[vi].x, tri.p[k][1] = g.vertices[vi].y, tri.p[k][2] = g.vertices[vi].z;
            }
            if (ok) {
                tri.colour = colour;
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

void addPlacedMesh(Scene& scene, const PreparedMesh& mesh, double x, double y, double z, double yaw) {
    const double c = std::cos(yaw), s = std::sin(yaw);
    for (const auto& t : mesh.triangles) {
        SceneTriangle st;
        for (int k = 0; k < 3; ++k) {
            const double lx = t.p[k][0], ly = t.p[k][1];
            st.p[k] = {x + c * lx - s * ly, y + s * lx + c * ly, z + t.p[k][2]};
            st.c[k] = t.colour;
        }
        scene.triangles.push_back(st);
    }
}

}  // namespace trainz::view
