// route-tool: look at a route's mapfile.gnd (terrain sections), read-only.
//
//   route-tool info     ARCHIVE.ja ROUTE_FOLDER          sections, positions, sizes
//   route-tool overview ARCHIVE.ja ROUTE_FOLDER [--out DIR]
//
// ROUTE_FOLDER is the route asset's folder inside the archive, e.g.
// "hash-fa/kuid 447264 1035". "overview" stitches the 64x64 overview map of
// every section into one PNG (colours and orientation not confirmed), written
// below ~/.local/share/trainz-native/routes.
#include "common/DataDirs.h"
#include "common/PngWriter.h"
#include "common/SafeOutput.h"
#include "content/ContentError.h"
#include "content/GroundFile.h"
#include "content/JaArchive.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace trainz::content;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage:\n  route-tool info     ARCHIVE.ja ROUTE_FOLDER\n"
                     "  route-tool overview ARCHIVE.ja ROUTE_FOLDER [--out DIR]\n";
        return 2;
    }
    const std::string cmd = argv[1];
    fs::path outDir;
    if (argc == 6 && std::string(argv[4]) == "--out") {
        outDir = argv[5];
    } else if (argc != 4) {
        std::cerr << "unexpected arguments\n";
        return 2;
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
            std::printf("%s: version %u, %zu sections, %zu bytes of world data (not decoded)\n", e->name.c_str(),
                        static_cast<unsigned>(g.version), g.sections.size(), g.extraHeader.size());
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
        if (cmd != "overview") {
            std::cerr << "unknown command\n";
            return 2;
        }
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
        const auto w = static_cast<std::uint32_t>(spanX * 64), h = static_cast<std::uint32_t>(spanY * 64);
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4, 0);
        for (std::size_t i = 3; i < rgba.size(); i += 4) {
            rgba[i] = 255;
        }
        for (const auto& s : g.sections) {
            for (std::size_t j = 0; j < s.overview.size(); ++j) {
                const std::size_t px = static_cast<std::size_t>(static_cast<long>(s.x) - minX) * 64 + j % 64;
                const std::size_t py = static_cast<std::size_t>(static_cast<long>(maxY) - s.y) * 64 + j / 64;
                std::uint8_t* d = rgba.data() + (py * w + px) * 4;
                overviewToRgb(s.overview[j], d[0], d[1], d[2]);
            }
        }
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
        file += ".overview.png";
        out.createFile(file, [&](int fd) {
            std::size_t done = 0;
            while (done < png.size()) {
                const ssize_t n = ::write(fd, png.data() + done, png.size() - done);
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    throw std::runtime_error(std::string("write failed: ") + std::strerror(errno));
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
