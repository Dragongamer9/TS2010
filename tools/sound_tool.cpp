// sound-tool: inspect .wav and .mp3 sounds and export them as plain 16-bit PCM WAV.
//
//   sound-tool info   ARCHIVE.ja NAME
//   sound-tool export ARCHIVE.ja NAME [--out DIR]
//
// MP3 (a .mp3 file, or MP3 inside a .wav) needs a build with libmpg123.
//
// Game files are only read; exports go below ~/.local/share/trainz-native/sounds
// (or --out inside it).
#include "common/DataDirs.h"
#include "common/SafeOutput.h"
#include "content/ContentError.h"
#include "content/JaArchive.h"
#include "content/SoundFile.h"

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
    if (argc != 4 && argc != 6) {
        std::cerr << "usage:\n  sound-tool info   ARCHIVE.ja NAME\n  sound-tool export ARCHIVE.ja NAME [--out DIR]\n";
        return 2;
    }
    const std::string cmd = argv[1];
    fs::path outDir;
    if (argc == 6) {
        if (std::string(argv[4]) != "--out") {
            std::cerr << "unexpected argument " << argv[4] << "\n";
            return 2;
        }
        outDir = argv[5];
    }
    try {
        const auto archive = JaArchive::open(argv[2]);
        const JaEntry* e = archive.find(argv[3]);
        if (e == nullptr) {
            std::cerr << "'" << argv[3] << "' is not in " << argv[2] << "\n";
            return 1;
        }
        const auto bytes = archive.readLimited(*e, std::size_t{256} << 20);
        const bool isMp3 = e->name.size() >= 4 && e->name.compare(e->name.size() - 4, 4, ".mp3") == 0;
        if (cmd == "info" && isMp3) {
            const PcmSound s = decodeMp3(bytes);
            for (const auto& m : s.warnings) {
                std::cerr << "warning: " << m << "\n";
            }
            std::printf("%s: MP3, %u channel(s), %u Hz, %zu sample frames (%.3f s), %zu bytes\n", e->name.c_str(),
                        s.channels, s.sampleRate, s.samples.size() / s.channels,
                        static_cast<double>(s.samples.size() / s.channels) / s.sampleRate, bytes.size());
            return 0;
        }
        if (cmd == "info") {
            const WavInfo w = parseWav(bytes);
            for (const auto& m : w.warnings) {
                std::cerr << "warning: " << m << "\n";
            }
            std::printf("%s: format tag %u, %u channel(s), %u Hz, %u bits, %zu bytes of sound data\n", e->name.c_str(),
                        w.formatTag, w.channels, w.sampleRate, w.bitsPerSample, w.dataSize);
            for (const auto& c : w.otherChunks) {
                std::printf("  other chunk '%s'\n", c.c_str());
            }
            return 0;
        }
        if (cmd != "export") {
            std::cerr << "unknown command\n";
            return 2;
        }
        const PcmSound s = decodeSoundFile(e->name, bytes);
        for (const auto& m : s.warnings) {
            std::cerr << "warning: " << m << "\n";
        }
        const auto wav = encodePcmWav(s);
        if (outDir.empty()) {
            outDir = trainz::common::portDataDir() / "sounds";
        }
        const auto out = trainz::common::SafeOutputDir::open(outDir, trainz::common::protectedDirsFor(argv[2]));
        std::string file = e->name;
        for (char& c : file) {
            if (c == '/' || c == '\\' || c == ':') {
                c = '_';
            }
        }
        if (isMp3) {
            file += ".wav";  // the export is a PCM WAV
        }
        out.createFile(file, [&](int fd) {
            std::size_t done = 0;
            while (done < wav.size()) {
                const ssize_t n = ::write(fd, wav.data() + done, wav.size() - done);
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    throw std::runtime_error(std::string("write failed: ") + std::strerror(errno));
                }
                done += static_cast<std::size_t>(n);
            }
        });
        std::printf("wrote %s (%zu samples, %u Hz, %u channel(s))\n", (out.path() / file).c_str(), s.samples.size(),
                    s.sampleRate, s.channels);
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
}
