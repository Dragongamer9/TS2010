// route-tool: look at a route's mapfile.gnd (terrain sections), read-only.
//
//   route-tool info     ARCHIVE.ja ROUTE_FOLDER          sections, positions, sizes
//   route-tool overview ARCHIVE.ja ROUTE_FOLDER [--out DIR]
//   route-tool heights  ARCHIVE.ja ROUTE_FOLDER [--out DIR]
//   route-tool track    ARCHIVE.ja ROUTE_FOLDER [--out DIR]   heights + the track network on top
//
// ROUTE_FOLDER is the route asset's folder inside the archive, e.g.
// "hash-fa/kuid 447264 1035". "overview" stitches the 64x64 overview map of
// every section into one PNG (colours and orientation not confirmed), written
// below ~/.local/share/trainz-native/routes.
#include "common/DataDirs.h"
#include "common/PngWriter.h"
#include "common/SafeOutput.h"
#include "content/ContentError.h"
#include "content/AssetIndex.h"
#include "content/GroundFile.h"
#include "content/ObjectContainer.h"
#include "content/PlacedObject.h"
#include "content/SessionFile.h"
#include "view/RouteScene.h"
#include "view/SceneRender.h"
#include "content/TrackLayout.h"
#include "content/JaArchive.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace trainz::content;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage:\n  route-tool info     ARCHIVE.ja ROUTE_FOLDER\n"
                     "  route-tool overview ARCHIVE.ja ROUTE_FOLDER [--out DIR]\n"
                     "  route-tool heights  ARCHIVE.ja ROUTE_FOLDER [--out DIR]\n"
                     "  route-tool track    ARCHIVE.ja ROUTE_FOLDER [--session ARCHIVE.ja SESSION_FOLDER] [--out DIR]\n"
                     "  route-tool view     ARCHIVE.ja ROUTE_FOLDER [--session ARCHIVE.ja SESSION_FOLDER] [--at X Y]\n"
                     "                      [--radius M] [--install DIR] [--out DIR]   a 3D view of part of the route\n"
                     "                      (--install: the TS2010 folder, to draw objects' 3D models)\n";
        return 2;
    }
    const std::string cmd = argv[1];
    fs::path outDir;
    std::string sessionArchive, sessionFolder;
    std::optional<double> atX, atY;
    double radius = 900.0;
    fs::path installDir;
    for (int i = 4; i < argc; ++i) {
        const std::string opt = argv[i];
        if (opt == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        } else if (opt == "--at" && i + 2 < argc && cmd == "view") {
            atX = std::stod(argv[++i]);
            atY = std::stod(argv[++i]);
        } else if (opt == "--install" && i + 1 < argc && cmd == "view") {
            installDir = argv[++i];
        } else if (opt == "--radius" && i + 1 < argc && cmd == "view") {
            radius = std::stod(argv[++i]);
        } else if (opt == "--session" && i + 2 < argc && (cmd == "track" || cmd == "view")) {
            sessionArchive = argv[++i];
            sessionFolder = argv[++i];
        } else {
            std::cerr << "unexpected argument " << opt << "\n";
            return 2;
        }
    }
    try {
        const auto archive = JaArchive::open(argv[2]);
        const std::string gndName = std::string(argv[3]) + "/mapfile.gnd";
        const JaEntry* e = archive.find(gndName);
        if (e == nullptr) {
            std::cerr << "'" << gndName << "' is not in " << argv[2] << "\n";
            return 1;
        }
        const GroundFile g = parseGround(archive.readLimited(*e, std::size_t{1} << 30));
        for (const auto& w : g.warnings) {
            std::cerr << "warning: " << w << "\n";
        }
        if (cmd == "info") {
            std::printf("%s: version %u, %zu sections, %zu bytes of world data\n", e->name.c_str(),
                        static_cast<unsigned>(g.version), g.sections.size(), g.extraHeader.size());
            if (g.worldDecoded) {
                const auto& w = g.world;
                std::printf("  world: %zu environment keys, environment %s, kuid %s, %zu objects", w.envKeys.size(),
                            formatKuid(w.envKuid.normalized()).c_str(), formatKuid(w.kuid.normalized()).c_str(),
                            w.items.size());
                if (w.hasDate) {
                    std::printf(", date %lld", static_cast<long long>(w.date));
                }
                std::printf("\n");
            }
            for (const auto& s : g.sections) {
                const auto d = parseGroundSection(s, g.version);
                std::size_t used = 0;
                float lo = 0, hi = 0;
                for (const auto& t : d.textures) {
                    used += t.used ? 1 : 0;
                }
                for (std::size_t i = 0; i < d.vertices.size(); ++i) {
                    const float h = d.vertices[i].height;
                    lo = i == 0 ? h : std::min(lo, h);
                    hi = i == 0 ? h : std::max(hi, h);
                }
                std::printf("  %s at (%d, %d): %zu bytes, %zu textures, %zu height cells, %zu vertices, height %.2f..%.2f\n",
                            s.tag.c_str(), s.x, s.y, s.data.size(), used, d.cells.size(), d.vertices.size(),
                            static_cast<double>(lo), static_cast<double>(hi));
            }
            return 0;
        }
        if (cmd != "overview" && cmd != "heights" && cmd != "track" && cmd != "view") {
            std::cerr << "unknown command\n";
            return 2;
        }
        std::uint32_t w = 0, h = 0;
        std::vector<std::uint8_t> rgba;
        if (cmd == "view") {
            // A 3D view of part of the route: terrain coloured from its overview maps, track,
            // placed objects and (with --session) trains. Only decoded data; no meshes.
            TrackLayout track;
            bool haveTrack = false;
            if (const JaEntry* te = archive.find(std::string(argv[3]) + "/mapfile.trk")) {
                track = readTrackLayout(parseRouteObjectFile(archive.readLimited(*te, std::size_t{256} << 20)));
                haveTrack = true;
            }
            std::vector<PlacedObject> objects;
            if (const JaEntry* oe = archive.find(std::string(argv[3]) + "/mapfile.obs")) {
                for (const auto& c : parseRouteObjectFile(archive.readLimited(*oe, std::size_t{256} << 20)).containers) {
                    for (const auto& item : c.items) {
                        if (auto o = decodePlacedObject(item)) {
                            objects.push_back(std::move(*o));
                        }
                    }
                }
            }
            std::vector<Train> trains;
            if (!sessionArchive.empty()) {
                const auto sa = JaArchive::open(sessionArchive);
                const JaEntry* pe = sa.find(sessionFolder + "/profile.dat");
                if (pe == nullptr) {
                    throw ContentError("'" + sessionFolder + "/profile.dat' is not in " + sessionArchive);
                }
                for (const auto& blk : parseSessionProfile(sa.readLimited(*pe, std::size_t{256} << 20)).blocks) {
                    if (blk.container) {
                        for (const auto& item : blk.container->items) {
                            if (auto t = decodeTrain(item)) {
                                trains.push_back(std::move(*t));
                            }
                        }
                    }
                }
            }
            if (!(radius >= 50 && radius <= 20000)) {
                throw ContentError("--radius must be 50..20000 metres");
            }
            std::optional<AssetIndex> index;
            std::optional<trainz::view::AssetMeshes> meshes;
            if (!installDir.empty()) {
                index = AssetIndex::scanInstall(installDir);
                meshes.emplace(*index);
            }
            trainz::view::RouteSceneInput in;
            in.meshes = meshes ? &*meshes : nullptr;
            in.ground = &g;
            in.track = haveTrack ? &track : nullptr;
            in.objects = &objects;
            in.trains = sessionArchive.empty() ? nullptr : &trains;
            const auto view = trainz::view::buildRouteView(in, radius, atX, atY);
            w = 1280, h = 720;
            rgba = trainz::view::renderScene(view.scene, view.camera, w, h);
            std::printf("view: centre %.0f, %.0f, radius %.0f m; %zu sections, %zu triangles, %zu track lines, "
                        "%zu dots (objects green, signals magenta, trains orange); objects as models %zu, as dots %zu; "
                        "straight track\n",
                        view.centreX, view.centreY, radius, view.sections, view.scene.triangles.size(),
                        view.scene.lines.size(), view.scene.dots.size(), view.objectsAsMeshes, view.objectsAsDots);
            if (meshes) {
                for (const auto& [why, n] : meshes->misses()) {
                    std::printf("  assets without a model: %zu (%s)\n", n, why.c_str());
                }
                for (const auto& [what, n] : meshes->parts()) {
                    std::printf("  %s: %zu\n", what.c_str(), n);
                }
            }
        } else {
        int minX = g.sections[0].x, maxX = minX, minY = g.sections[0].y, maxY = minY;
        for (const auto& s : g.sections) {
            minX = std::min(minX, s.x), maxX = std::max(maxX, s.x);
            minY = std::min(minY, s.y), maxY = std::max(maxY, s.y);
        }
        const long spanX = static_cast<long>(maxX) - minX + 1, spanY = static_cast<long>(maxY) - minY + 1;
        if (spanX * 64 > 8192 || spanY * 64 > 8192) {
            std::cerr << "route is too large for one overview image\n";
            return 1;
        }
        w = static_cast<std::uint32_t>(spanX * 64), h = static_cast<std::uint32_t>(spanY * 64);
        rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
        if (cmd == "heights" || cmd == "track") {
            // Grid layout (observed on the disc, not traced): vertex = row * side + column;
            // columns run toward +x and rows toward +y; a section spans side - 4 cells
            // (72 or 144), sharing its edge vertices with the next section (column/row
            // side - 4 equals the neighbour's 0 on 81% of disc edges; the other 19% differ,
            // unexplained, so seams remain there). The last 3 rows and
            // columns lie past the edge and are not drawn. One 64x64 tile per section;
            // the image's top is +y, as in "overview".
            double low = std::numeric_limits<double>::infinity();
            double high = -low;
            std::size_t invalid = 0;
            for (const auto& section : g.sections) {
                const auto decoded = parseGroundSection(section, g.version);
                for (const auto& vertex : decoded.vertices) {
                    if (!std::isfinite(vertex.height)) {
                        ++invalid;
                        continue;
                    }
                    low = std::min(low, static_cast<double>(vertex.height));
                    high = std::max(high, static_cast<double>(vertex.height));
                }
            }
            if (!std::isfinite(low)) {
                throw ContentError("route has no finite terrain heights");
            }
            for (const auto& section : g.sections) {
                const auto decoded = parseGroundSection(section, g.version);
                const std::size_t side = groundVertexSide(section.tag);
                if (side < 8 || decoded.vertices.size() != side * side) {
                    throw ContentError("terrain vertex grid has an unexpected size");
                }
                const std::size_t tileX = static_cast<std::size_t>(static_cast<long>(section.x) - minX) * 64;
                const std::size_t tileY = static_cast<std::size_t>(static_cast<long>(maxY) - section.y) * 64;
                for (std::size_t y = 0; y < 64; ++y) {
                    for (std::size_t x = 0; x < 64; ++x) {
                        const std::size_t cells = side - 4;
                        const std::size_t vx = (x * cells + 31) / 63;
                        const std::size_t vy = cells - (y * cells + 31) / 63;  // image top = +y
                        const double heightValue = decoded.vertices[vy * side + vx].height;
                        if (!std::isfinite(heightValue)) {
                            continue;  // non-finite samples and missing sections stay transparent
                        }
                        const double scaled = high > low ? (heightValue - low) / (high - low) : 0.5;
                        const auto grey = static_cast<std::uint8_t>(std::clamp(scaled, 0.0, 1.0) * 255.0 + 0.5);
                        auto* pixel = rgba.data() + ((tileY + y) * w + tileX + x) * 4;
                        pixel[0] = grey, pixel[1] = grey, pixel[2] = grey, pixel[3] = 255;
                    }
                }
            }
            std::printf("height range: %.9g..%.9g (black..white; flat routes use grey); %zu non-finite vertices\n",
                        low, high, invalid);
            std::cerr << "note: grid orientation, edge sharing and the 10 m spacing are observed, not traced\n";
            if (cmd == "track") {
                // The track network from mapfile.trk (layout observed; see docs/formats/route-objects.md).
                const std::string trkName = std::string(argv[3]) + "/mapfile.trk";
                const JaEntry* te = archive.find(trkName);
                if (te == nullptr) {
                    throw ContentError("'" + trkName + "' is not in the archive");
                }
                const TrackLayout t = readTrackLayout(parseRouteObjectFile(archive.readLimited(*te, std::size_t{256} << 20)));
                for (const auto& warning : t.warnings) {
                    std::cerr << "warning: " << warning << "\n";
                }
                // World metres to pixels: 64 pixels per 720 m baseboard; image top = +y.
                auto toPixel = [&](const TrackVertex& v, double& px, double& py) {
                    px = (v.worldX() / kBaseboardSize - minX) * 64.0;
                    py = (static_cast<double>(maxY) + 1.0 - v.worldY() / kBaseboardSize) * 64.0;
                };
                auto plot = [&](double px, double py, std::uint8_t r, std::uint8_t gr, std::uint8_t b) {
                    if (!(px >= 0 && py >= 0 && px < w && py < h)) {
                        return;  // off the terrain image (or not finite)
                    }
                    auto* pixel = rgba.data() + (static_cast<std::size_t>(py) * w + static_cast<std::size_t>(px)) * 4;
                    pixel[0] = r, pixel[1] = gr, pixel[2] = b, pixel[3] = 255;
                };
                // Placed objects first, so the track stays on top: dark green dots, signals magenta.
                std::size_t objects = 0;
                const JaEntry* oe = archive.find(std::string(argv[3]) + "/mapfile.obs");
                if (oe != nullptr) {
                    const auto obs = parseRouteObjectFile(archive.readLimited(*oe, std::size_t{256} << 20));
                    for (const auto& c : obs.containers) {
                        for (const auto& item : c.items) {
                            const auto o = decodePlacedObject(item);
                            if (!o) {
                                continue;
                            }
                            const double px = (o->worldX() / kBaseboardSize - minX) * 64.0;
                            const double py = (static_cast<double>(maxY) + 1.0 - o->worldY() / kBaseboardSize) * 64.0;
                            if (o->tag == "sgOb") {
                                plot(px, py, 230, 40, 230);
                                plot(px + 1, py, 230, 40, 230);
                            } else {
                                plot(px, py, 20, 120, 40);
                            }
                            ++objects;
                        }
                    }
                }
                std::size_t drawn = 0;
                for (const auto& st : t.stretches) {
                    const auto a = t.vertices.find(st.from);
                    const auto b = t.vertices.find(st.to);
                    if (a == t.vertices.end() || b == t.vertices.end()) {
                        continue;
                    }
                    double x0, y0, x1, y1;
                    toPixel(a->second, x0, y0);
                    toPixel(b->second, x1, y1);
                    const double len = std::hypot(x1 - x0, y1 - y0);
                    if (!std::isfinite(len) || len > 4.0 * w + 4.0 * h) {
                        continue;
                    }
                    const auto steps = static_cast<std::size_t>(std::ceil(len)) + 1;
                    const bool wrapped = st.kind == "tkBr";
                    for (std::size_t k = 0; k <= steps; ++k) {
                        const double f = static_cast<double>(k) / static_cast<double>(steps);
                        plot(x0 + (x1 - x0) * f, y0 + (y1 - y0) * f, 230, wrapped ? 210 : 30, 30);
                    }
                    ++drawn;
                }
                std::size_t junctions = 0;
                for (const auto& [index, v] : t.vertices) {
                    if (v.links.size() >= 3) {
                        double px, py;
                        toPixel(v, px, py);
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                plot(px + dx, py + dy, 0, 220, 240);
                            }
                        }
                        ++junctions;
                    }
                }
                // A session's trains on top: every vehicle at its own stored position, orange.
                std::size_t trains = 0, vehicles = 0;
                if (!sessionArchive.empty()) {
                    const auto sa = JaArchive::open(sessionArchive);
                    const std::string profName = sessionFolder + "/profile.dat";
                    const JaEntry* pe = sa.find(profName);
                    if (pe == nullptr) {
                        throw ContentError("'" + profName + "' is not in " + sessionArchive);
                    }
                    const auto prof = parseSessionProfile(sa.readLimited(*pe, std::size_t{256} << 20));
                    for (const auto& blk : prof.blocks) {
                        if (!blk.container) {
                            continue;
                        }
                        for (const auto& item : blk.container->items) {
                            const auto train = decodeTrain(item);
                            if (!train) {
                                continue;
                            }
                            ++trains;
                            for (const auto& v : train->vehicles) {
                                const double px = ((v.boardX * kBaseboardSize + v.x) / kBaseboardSize - minX) * 64.0;
                                const double py =
                                    (static_cast<double>(maxY) + 1.0 - (v.boardY * kBaseboardSize + v.y) / kBaseboardSize) * 64.0;
                                for (int dy = -1; dy <= 1; ++dy) {
                                    for (int dx = -1; dx <= 1; ++dx) {
                                        plot(px + dx, py + dy, 255, 140, 0);
                                    }
                                }
                                ++vehicles;
                            }
                        }
                    }
                    std::printf("session: %zu trains, %zu vehicles (orange)\n", trains, vehicles);
                }
                std::printf("track: %zu pieces drawn (red: tkSt, yellow: tkBr), %zu vertices with 3+ links (cyan); "
                            "straight lines between vertices (curves not decoded); %zu placed objects (green, signals "
                            "magenta)\n",
                            drawn, junctions, objects);
            }
        } else {
            for (std::size_t i = 3; i < rgba.size(); i += 4) {
                rgba[i] = 255;
            }
            for (const auto& section : g.sections) {
                for (std::size_t j = 0; j < section.overview.size(); ++j) {
                    const std::size_t px = static_cast<std::size_t>(static_cast<long>(section.x) - minX) * 64 + j % 64;
                    const std::size_t py = static_cast<std::size_t>(static_cast<long>(maxY) - section.y) * 64 + j / 64;
                    auto* pixel = rgba.data() + (py * w + px) * 4;
                    overviewToRgb(section.overview[j], pixel[0], pixel[1], pixel[2]);
                }
            }
        }
        }  // map commands
        const auto png = trainz::common::encodePngRgba(w, h, rgba);
        if (outDir.empty()) {
            outDir = trainz::common::portDataDir() / "routes";
        }
        const auto out = trainz::common::SafeOutputDir::open(outDir, trainz::common::protectedDirsFor(argv[2]));
        std::string file = std::string(argv[3]);
        for (char& c : file) {
            if (c == '/' || c == '\\' || c == ':') {
                c = '_';
            }
        }
        file += (cmd == "heights" ? ".heights.png" : cmd == "track" ? ".track.png" : cmd == "view" ? ".view.png" : ".overview.png");
        out.createFile(file, [&](int fd) {
            std::size_t done = 0;
            while (done < png.size()) {
                const ssize_t n = ::write(fd, png.data() + done, png.size() - done);
                if (n <= 0) {
                    if (n < 0 && errno == EINTR) {
                        continue;
                    }
                    throw std::runtime_error(n == 0 ? "write made no progress"
                                                    : std::string("write failed: ") + std::strerror(errno));
                }
                done += static_cast<std::size_t>(n);
            }
        });
        std::printf("wrote %s (%ux%u)\n", (out.path() / file).c_str(), w, h);
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
}
