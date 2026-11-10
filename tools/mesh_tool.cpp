// mesh-tool: inspect .im / .pm meshes and export them for viewing.
//
//   mesh-tool info    ARCHIVE.ja NAME
//   mesh-tool preview ARCHIVE.ja NAME [--view side|front|top|3q] [--size WxH] [--out DIR]
//                     [--kin ANIM.kin --frame N]   pose animated parts (bone positions taken as absolute)
//   mesh-tool obj     ARCHIVE.ja NAME [--out DIR]
//
// "obj" writes NAME.obj + NAME.mtl (Y-up, for Blender and other viewers) and
// converts the textures the materials name, when they are in the same folder
// of the archive, to PNG next to it. Game files are only read; everything is
// written below ~/.local/share/trainz-native/meshes (or --out inside it).
#include "common/DataDirs.h"
#include "common/PngWriter.h"
#include "common/SafeOutput.h"
#include "content/AnimationFile.h"
#include "content/ContentError.h"
#include "content/JaArchive.h"
#include "content/MeshFile.h"
#include "content/TextureFile.h"
#include "view/MeshPreview.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace trainz::content;

namespace {

constexpr std::size_t kMaxFile = std::size_t{256} << 20;

int usage() {
    std::cerr << "usage:\n"
                 "  mesh-tool info    ARCHIVE.ja NAME\n"
                 "  mesh-tool preview ARCHIVE.ja NAME [--view side|front|top|3q] [--size WxH] [--out DIR]\n"
                 "                    [--kin ANIM.kin --frame N]\n"
                 "  mesh-tool obj     ARCHIVE.ja NAME [--out DIR]\n";
    return 2;
}

std::string safeName(std::string s) {
    for (char& c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '"' || c == '\n' || c == '\r') {
            c = '_';
        }
    }
    if (s.empty() || s == "." || s == "..") {
        s = "mesh";
    }
    return s;
}

void writeAll(int fd, const void* data, std::size_t n) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::size_t done = 0;
    while (done < n) {
        const ssize_t w = ::write(fd, p + done, n - done);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("write failed: ") + std::strerror(errno));
        }
        done += static_cast<std::size_t>(w);
    }
}

void writeFile(const trainz::common::SafeOutputDir& out, const std::string& name, const std::string& text) {
    out.createFile(name, [&](int fd) { writeAll(fd, text.data(), text.size()); });
}

void writeFile(const trainz::common::SafeOutputDir& out, const std::string& name,
               const std::vector<std::uint8_t>& bytes) {
    out.createFile(name, [&](int fd) { writeAll(fd, bytes.data(), bytes.size()); });
}

std::string fmt(float f) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", static_cast<double>(f));
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        return usage();
    }
    const std::string cmd = argv[1];
    const fs::path archivePath = argv[2];
    const std::string name = argv[3];
    std::string viewName = "3q";
    std::uint32_t w = 640, h = 480;
    fs::path outDir;
    std::string kinName;
    long frame = 0;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--kin" && i + 1 < argc) {
            kinName = argv[++i];
        } else if (a == "--frame" && i + 1 < argc) {
            frame = std::strtol(argv[++i], nullptr, 10);
            if (frame < 0) {
                return usage();
            }
        } else if (a == "--view" && i + 1 < argc) {
            viewName = argv[++i];
        } else if (a == "--size" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%ux%u", &w, &h) != 2 || w == 0 || h == 0 || w > 4096 || h > 4096) {
                return usage();
            }
        } else if (a == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        } else {
            return usage();
        }
    }
    try {
        const auto archive = JaArchive::open(archivePath);
        const JaEntry* e = archive.find(name);
        if (e == nullptr) {
            std::cerr << "'" << name << "' is not in " << archivePath.string() << "\n";
            return 1;
        }
        MeshFile mesh = parseMesh(archive.readLimited(*e, kMaxFile));
        for (const auto& wmsg : mesh.warnings) {
            std::cerr << "warning: " << wmsg << "\n";
        }
        const std::string base = safeName(fs::path(e->name).stem().string());

        if (cmd == "info") {
            std::size_t tris = 0, verts = 0;
            for (const auto& p : mesh.parts) {
                tris += p.geometry.indices.size() / 3;
                verts += p.geometry.vertices.size();
            }
            std::printf("%s: %zu parts, %zu vertices, %zu triangles\n", e->name.c_str(), mesh.parts.size(), verts, tris);
            for (const auto& c : mesh.otherChunks) {
                std::printf("  extra chunk %s\n", c.c_str());
            }
            for (const auto& p : mesh.parts) {
                std::printf("  part %u: GEOM v%u, %zu vertices, %zu triangles%s%s, material \"%s\" (%s)\n", p.index,
                            p.geometry.version, p.geometry.vertices.size(), p.geometry.indices.size() / 3,
                            p.geometry.boneName.empty() ? "" : ", bone ", p.geometry.boneName.c_str(),
                            p.material.name.c_str(), p.material.kind.c_str());
                for (const auto& t : p.material.textures) {
                    std::printf("      texture type %u: %s (amount %s)\n", t.type, t.fileName.c_str(), fmt(t.amount).c_str());
                }
            }
            return 0;
        }

        if (outDir.empty()) {
            outDir = trainz::common::portDataDir() / "meshes";
        }
        const auto out = trainz::common::SafeOutputDir::open(outDir, trainz::common::protectedDirsFor(archivePath));

        if (cmd == "preview") {
            trainz::view::PreviewView view = trainz::view::PreviewView::ThreeQuarter;
            if (viewName == "side") {
                view = trainz::view::PreviewView::Side;
            } else if (viewName == "front") {
                view = trainz::view::PreviewView::Front;
            } else if (viewName == "top") {
                view = trainz::view::PreviewView::Top;
            } else if (viewName != "3q") {
                return usage();
            }
            std::string suffix;
            if (!kinName.empty()) {
                const JaEntry* ke = archive.find(kinName);
                if (ke == nullptr) {
                    std::cerr << "'" << kinName << "' is not in " << archivePath.string() << "\n";
                    return 1;
                }
                const auto anim = parseAnimation(archive.readLimited(*ke, kMaxFile));
                const std::size_t moved = trainz::view::poseMesh(mesh, anim, static_cast<std::size_t>(frame));
                std::printf("posed %zu part(s) at frame %ld of %u\n", moved, frame, anim.frameCount);
                suffix = ".f" + std::to_string(frame);
            }
            const auto rgba = trainz::view::renderMeshPreview(mesh, w, h, view);
            const std::string file = base + suffix + "." + viewName + ".png";
            writeFile(out, file, trainz::common::encodePngRgba(w, h, rgba));
            std::printf("wrote %s\n", (out.path() / file).c_str());
            return 0;
        }

        if (cmd != "obj") {
            return usage();
        }
        // OBJ: Jet is Z-up, OBJ viewers expect Y-up, so (x, y, z) -> (x, z, -y).
        // Texture v is flipped (DirectX-style top-left origin; not confirmed).
        const std::string folder = e->name.find('/') == std::string::npos ? "" : e->name.substr(0, e->name.rfind('/'));
        std::string obj = "# exported by mesh-tool from " + e->name + "\nmtllib " + base + ".mtl\n";
        std::string mtl;
        std::set<std::string> converted;
        std::size_t vBase = 1;
        for (const auto& p : mesh.parts) {
            const auto& g = p.geometry;
            const std::string mtlName = "part" + std::to_string(p.index);
            for (const auto& v : g.vertices) {
                obj += "v " + fmt(v.x) + " " + fmt(v.z) + " " + fmt(-v.y) + "\n";
            }
            for (const auto& v : g.vertices) {
                obj += "vt " + fmt(v.u) + " " + fmt(1.0f - v.v) + "\n";
            }
            for (const auto& n : g.vertexNormals) {
                obj += "vn " + fmt(n[0]) + " " + fmt(n[2]) + " " + fmt(-n[1]) + "\n";
            }
            obj += "o " + mtlName + "\nusemtl " + mtlName + "\n";
            for (std::size_t t = 0; t + 2 < g.indices.size(); t += 3) {
                obj += "f";
                for (int k = 0; k < 3; ++k) {
                    const std::string i = std::to_string(vBase + g.indices[t + static_cast<std::size_t>(k)]);
                    obj += " " + i + "/" + i + "/" + i;
                }
                obj += "\n";
            }
            vBase += g.vertices.size();

            mtl += "newmtl " + mtlName + "\n# " + safeName(p.material.name) + "\nKd 1 1 1\n";
            for (const auto& t : p.material.textures) {
                if (t.type != 1 || t.fileName.empty()) {
                    continue;
                }
                const std::string texEntry = (folder.empty() ? "" : folder + "/") + t.fileName;
                const JaEntry* te = archive.find(texEntry);
                if (te == nullptr) {
                    mtl += "# texture " + safeName(t.fileName) + " is not in this folder of the archive\n";
                    break;
                }
                const std::string png = safeName(fs::path(t.fileName).stem().string()) + ".png";
                if (!converted.count(png)) {
                    try {
                        const auto tex = parseTexture(archive.readLimited(*te, kMaxFile));
                        const auto rgba = decodeToRgba8(tex.mips.front(), tex.info.format);
                        writeFile(out, png, trainz::common::encodePngRgba(tex.mips.front().width,
                                                                          tex.mips.front().height, rgba));
                        converted.insert(png);
                    } catch (const std::exception& err) {
                        std::cerr << "texture " << t.fileName << ": " << err.what() << "\n";
                        break;
                    }
                }
                mtl += "map_Kd " + png + "\n";
                break;
            }
        }
        writeFile(out, base + ".obj", obj);
        writeFile(out, base + ".mtl", mtl);
        std::printf("wrote %s (+ .mtl and %zu texture(s))\n", (out.path() / (base + ".obj")).c_str(), converted.size());
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
}
