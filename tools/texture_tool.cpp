// texture-tool: inspect .texture files (and loose .tga / .bmp / .jpg images)
// and convert them to PNG for viewing.
//
//   texture-tool info FILE                        a loose .texture, .tga, .bmp, .jpg/.jpeg
//   texture-tool info ARCHIVE.ja NAME             one inside a .ja archive
//   texture-tool png  ARCHIVE.ja NAME [--mip N] [--out DIR]
//   texture-tool png  FILE [--mip N] [--out DIR]
//
// Game files are only read. PNGs go to ~/.local/share/trainz-native/textures/
// (or --out, which must also be inside ~/.local/share/trainz-native).
#include "common/DataDirs.h"
#include "common/PngWriter.h"
#include "common/SafeOutput.h"
#include "content/ContentError.h"
#include "content/ImageFile.h"
#include "content/JaArchive.h"
#include "content/ReadOnlyFile.h"
#include "content/TextureFile.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace trainz::content;

namespace {

constexpr std::size_t kMaxTextureFile = std::size_t{128} << 20;

int usage() {
    std::cerr << "usage:\n"
                 "  texture-tool info FILE | ARCHIVE.ja NAME\n"
                 "  texture-tool png  FILE | ARCHIVE.ja NAME  [--mip N] [--out DIR]\n"
                 "  (FILE / NAME: .texture, .tga, .bmp, .jpg or .jpeg; --mip is for .texture only)\n";
    return 2;
}

std::string safeName(std::string s) {
    for (char& c : s) {
        if (c == '/' || c == '\\' || c == ':') {
            c = '_';
        }
    }
    if (s.empty() || s == "." || s == "..") {
        s = "texture";
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        return usage();
    }
    const std::string cmd = argv[1];
    std::vector<std::string> pos;
    int mipLevel = 0;
    fs::path outDir;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--mip" && i + 1 < argc) {
            mipLevel = std::atoi(argv[++i]);
        } else if (a == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        } else if (a.rfind("--", 0) == 0) {
            return usage();
        } else {
            pos.push_back(a);
        }
    }
    if ((cmd != "info" && cmd != "png") || pos.empty() || pos.size() > 2) {
        return usage();
    }
    try {
        std::vector<std::uint8_t> bytes;
        std::string shownName;
        fs::path sourcePath = pos[0];
        if (pos.size() == 2) {
            const auto archive = JaArchive::open(pos[0]);
            const JaEntry* e = archive.find(pos[1]);
            if (e == nullptr) {
                std::cerr << "'" << pos[1] << "' is not in " << pos[0] << "\n";
                return 1;
            }
            bytes = archive.readLimited(*e, kMaxTextureFile);
            shownName = e->name;
        } else {
            const auto f = ReadOnlyFile::open(pos[0]);
            if (f.size() > kMaxTextureFile) {
                std::cerr << "file is too large for a texture\n";
                return 1;
            }
            bytes.resize(static_cast<std::size_t>(f.size()));
            f.readExact(0, bytes);
            shownName = fs::path(pos[0]).filename().string();
        }
        std::uint32_t outW = 0;
        std::uint32_t outH = 0;
        std::vector<std::uint8_t> rgba;
        const ImageKind kind = imageKindFromName(shownName);
        if (kind != ImageKind::Unknown) {
            // .tga / .bmp / .jpg: one image, no mip levels.
            RgbaImage img = decodeImage(kind, bytes);
            for (const auto& w : img.warnings) {
                std::cerr << "warning: " << w << "\n";
            }
            if (cmd == "info") {
                std::printf("%s\n  %ux%u %s%s\n", shownName.c_str(), img.width, img.height, img.description.c_str(),
                            img.alphaFromFile ? ", alpha from the file" : "");
                return 0;
            }
            if (mipLevel != 0) {
                std::cerr << "--mip applies to .texture files only\n";
                return 2;
            }
            outW = img.width;
            outH = img.height;
            rgba = std::move(img.rgba);
        }
        const TextureFile tex = kind != ImageKind::Unknown ? TextureFile{} : parseTexture(bytes);
        for (const auto& w : tex.warnings) {
            std::cerr << "warning: " << w << "\n";
        }
        if (kind == ImageKind::Unknown && cmd == "info") {
            std::printf("%s\n  %ux%u %s, %zu mip levels, INFO version %u\n  INFO words:", shownName.c_str(),
                        tex.info.width, tex.info.height, pixelFormatName(tex.info.format), tex.mips.size(),
                        tex.info.version);
            for (std::size_t i = 0; i < tex.info.words.size(); ++i) {
                std::printf(" %u", tex.info.words[i]);
            }
            std::printf("\n");
            return 0;
        }
        if (kind == ImageKind::Unknown) {
            if (mipLevel < 0 || static_cast<std::size_t>(mipLevel) >= tex.mips.size()) {
                std::cerr << "--mip must be 0.." << tex.mips.size() - 1 << "\n";
                return 2;
            }
            const MipLevel& mip = tex.mips[static_cast<std::size_t>(mipLevel)];
            rgba = decodeToRgba8(mip, tex.info.format);
            outW = mip.width;
            outH = mip.height;
        }
        const auto png = trainz::common::encodePngRgba(outW, outH, rgba);

        if (outDir.empty()) {
            outDir = trainz::common::portDataDir() / "textures";
        }
        const auto out = trainz::common::SafeOutputDir::open(outDir, trainz::common::protectedDirsFor(sourcePath));
        std::string file = safeName(shownName);
        if (mipLevel > 0) {
            file += ".mip" + std::to_string(mipLevel);
        }
        file += ".png";
        out.createFile(file, [&](int fd) {
            std::size_t done = 0;
            while (done < png.size()) {
                const ssize_t w = ::write(fd, png.data() + done, png.size() - done);
                if (w < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    throw std::runtime_error(std::string("write failed: ") + std::strerror(errno));
                }
                done += static_cast<std::size_t>(w);
            }
        });
        std::printf("wrote %s (%ux%u)\n", (out.path() / file).c_str(), outW, outH);
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
}
