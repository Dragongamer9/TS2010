// anim-tool: show what a .kin animation contains (read-only).
//
//   anim-tool info ARCHIVE.ja NAME [--frames]
#include "content/AnimationFile.h"
#include "content/ContentError.h"
#include "content/JaArchive.h"

#include <cstdio>
#include <iostream>
#include <string>

using namespace trainz::content;

int main(int argc, char** argv) {
    if (argc < 4 || std::string(argv[1]) != "info" || argc > 5 || (argc == 5 && std::string(argv[4]) != "--frames")) {
        std::cerr << "usage: anim-tool info ARCHIVE.ja NAME [--frames]\n";
        return 2;
    }
    try {
        const auto archive = JaArchive::open(argv[2]);
        const JaEntry* e = archive.find(argv[3]);
        if (e == nullptr) {
            std::cerr << "'" << argv[3] << "' is not in " << argv[2] << "\n";
            return 1;
        }
        const auto a = parseAnimation(archive.readLimited(*e, std::size_t{256} << 20));
        std::printf("%s\n  source %s, %u frames at %u fps, %zu bones, %zu events\n", e->name.c_str(),
                    a.sourceFile.c_str(), a.frameCount, a.framesPerSecond, a.bones.size(), a.events.size());
        for (std::size_t i = 0; i < a.bones.size(); ++i) {
            int depth = 0;
            for (int p = a.bones[i].parent; p >= 0; p = a.bones[static_cast<std::size_t>(p)].parent) {
                ++depth;
            }
            std::printf("  %*sbone %zu: %s\n", 2 * depth, "", i, a.bones[i].name.c_str());
        }
        for (const auto& ev : a.events) {
            std::printf("  event at frame %u (flags %u): %s\n", ev.frame, ev.flags, ev.name.c_str());
        }
        if (argc == 5) {
            for (std::size_t f = 0; f < a.frames.size(); ++f) {
                for (std::size_t b = 0; b < a.bones.size(); ++b) {
                    const auto& p = a.frames[f][b];
                    std::printf("  frame %zu %s pos %.4f %.4f %.4f rot %.4f %.4f %.4f %.4f\n", f, a.bones[b].name.c_str(),
                                static_cast<double>(p.position[0]), static_cast<double>(p.position[1]),
                                static_cast<double>(p.position[2]), static_cast<double>(p.rotation[0]),
                                static_cast<double>(p.rotation[1]), static_cast<double>(p.rotation[2]),
                                static_cast<double>(p.rotation[3]));
                }
            }
        }
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
}
