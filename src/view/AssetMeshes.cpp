#include "AssetMeshes.h"

#include "content/ArchivePath.h"
#include "content/AssetConfig.h"
#include "content/ContentError.h"
#include "content/MeshFile.h"
#include "content/TextureFile.h"

#include <cmath>

namespace trainz::view {

using namespace trainz::content;

namespace {

constexpr std::size_t kMaxFileBytes = std::size_t{64} << 20;

std::string folderOf(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// The first mesh named in a config: "mesh" at the top, or the first "mesh" inside the
// first group of "mesh-table" (usually "default").
std::string firstMesh(const ConfigNode& root) {
    if (const auto* m = root.child("mesh"); m != nullptr && m->type == ConfigType::String) {
        return m->text;
    }
    if (const auto* table = root.child("mesh-table"); table != nullptr) {
        for (const auto& group : table->children) {
            if (const auto* m = group.child("mesh"); m != nullptr && m->type == ConfigType::String) {
                return m->text;
            }
        }
    }
    return {};
}

}  // namespace

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
    std::string mesh;
    try {
        mesh = firstMesh(parseAssetConfig(a->readLimited(*ce, std::size_t{16} << 20)).root);
    } catch (const ContentError&) {
        ++misses_["config unreadable"];
        return nullptr;
    }
    if (mesh.empty()) {
        ++misses_["no mesh in the config"];
        return nullptr;
    }
    for (char& c : mesh) {
        if (c == '\\') {
            c = '/';
        }
    }
    std::string meshPath = base + mesh;
    if (asciiLower(meshPath).ends_with(".lm")) {
        // .lm (level-of-detail list) is not read yet: use an .im of the same name if there is one.
        const std::string im = meshPath.substr(0, meshPath.size() - 3) + ".im";
        if (a->find(im) == nullptr) {
            ++misses_["only a .lm (LOD list, not read yet)"];
            return nullptr;
        }
        meshPath = im;
    }
    const JaEntry* me = a->find(meshPath);
    if (me == nullptr) {
        ++misses_["mesh file missing"];
        return nullptr;
    }
    MeshFile m;
    try {
        m = parseMeshOrRepair(a->readLimited(*me, kMaxFileBytes));
    } catch (const ContentError&) {
        ++misses_["mesh unreadable"];
        return nullptr;
    }
    auto out = std::make_unique<PreparedMesh>();
    out->meshName = meshPath;
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
            if (out->triangles.size() >= kMaxTrianglesPerMesh) {
                ++misses_["over the triangle limit"];
                return nullptr;
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
                out->triangles.push_back(tri);
            }
        }
    }
    if (out->triangles.empty()) {
        ++misses_["mesh has no triangles"];
        return nullptr;
    }
    return out;
}

const PreparedMesh* AssetMeshes::get(const Kuid& kuid) {
    const std::string key = formatKuid(kuid);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        std::unique_ptr<PreparedMesh> loaded;
        try {
            loaded = load(kuid);
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
