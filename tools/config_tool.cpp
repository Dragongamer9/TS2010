// config-tool: show a Trainz asset config (config.txt or config.chump), read-only.
//
//   config-tool show FILE                   a loose config.txt / config.chump
//   config-tool show ARCHIVE.ja NAME        one stored in a .ja archive
//   config-tool kuid FILE | ARCHIVE.ja NAME print only the asset's own KUID
//
// Nothing is ever written; files are opened read-only.
#include "content/AssetConfig.h"
#include "content/ContentError.h"
#include "content/JaArchive.h"
#include "content/Kuid.h"
#include "content/ReadOnlyFile.h"

#include <iostream>
#include <string>
#include <vector>

using namespace trainz::content;

namespace {

int usage() {
    std::cerr << "usage:\n"
                 "  config-tool show FILE\n"
                 "  config-tool show ARCHIVE.ja NAME\n"
                 "  config-tool kuid FILE\n"
                 "  config-tool kuid ARCHIVE.ja NAME\n";
    return 2;
}

std::vector<std::uint8_t> loadBytes(int argc, char** argv) {
    if (argc == 4) {
        const auto archive = JaArchive::open(argv[2]);
        const JaEntry* e = archive.find(argv[3]);
        if (e == nullptr) {
            throw ContentError(std::string("'") + argv[3] + "' is not in " + argv[2]);
        }
        return archive.read(*e);
    }
    const auto file = ReadOnlyFile::open(argv[2]);
    if (file.size() > (64u << 20)) {
        throw ContentError(std::string("'") + argv[2] + "' is too big to be a config file");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file.size()));
    file.readExact(0, bytes);
    return bytes;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        return usage();
    }
    const std::string cmd = argv[1];
    if (cmd != "show" && cmd != "kuid") {
        return usage();
    }
    try {
        const auto bytes = loadBytes(argc, argv);
        const AssetConfig cfg = parseAssetConfig(bytes);
        for (const auto& w : cfg.warnings) {
            std::cerr << "warning: " << w << "\n";
        }
        if (cmd == "kuid") {
            const auto k = cfg.assetKuid();
            if (!k) {
                std::cerr << "no readable top-level kuid\n";
                return 1;
            }
            std::cout << formatKuid(*k) << "\n";
            return 0;
        }
        std::cout << "// " << (cfg.format == ConfigFormat::Chump ? "config.chump" : "config.txt") << "\n";
        std::cout << formatConfigText(cfg.root);
        return 0;
    } catch (const ContentError& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
}
