// asset-tool: build the asset index of a Trainz install and query it, read-only.
//
//   asset-tool scan     INSTALL                  counts per source and kind, warnings
//   asset-tool find     INSTALL KUID             every copy of exactly this KUID
//   asset-tool versions INSTALL USER:CONTENT     every version of an asset
//   asset-tool list     INSTALL [KIND]           one line per asset
//
// INSTALL is the folder holding Base.ja, Data.ja, Builtin/ and UserData/.
// Nothing is written anywhere.
#include "content/AssetIndex.h"
#include "content/ContentError.h"
#include "content/Kuid.h"

#include <cstdio>
#include <iostream>
#include <map>
#include <string>

using namespace trainz::content;

namespace {

int usage() {
    std::cerr << "usage:\n"
                 "  asset-tool scan     INSTALL\n"
                 "  asset-tool find     INSTALL KUID\n"
                 "  asset-tool versions INSTALL USER:CONTENT\n"
                 "  asset-tool list     INSTALL [KIND]\n";
    return 2;
}

std::string where(const AssetRecord& r) {
    std::string s = r.where.source.filename().string();
    s += r.where.container == AssetLocation::Container::Archive ? ":" : "/";
    s += r.where.folder.empty() ? r.where.configName : r.where.folder + "/" + r.where.configName;
    return s;
}

void printRecord(const AssetRecord& r) {
    std::printf("%-28s %-14s %-40s %s\n", formatKuid(r.kuid).c_str(), r.kind.c_str(), r.username.c_str(),
                where(r).c_str());
    for (const auto& o : r.obsoletes) {
        std::printf("    obsoletes %s\n", formatKuid(o).c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        return usage();
    }
    const std::string cmd = argv[1];
    try {
        const AssetIndex index = AssetIndex::scanInstall(argv[2]);
        if (cmd == "scan" && argc == 3) {
            std::map<std::string, std::size_t> bySource;
            std::map<std::string, std::size_t> byKind;
            for (const auto& r : index.records()) {
                ++bySource[r.where.source.filename().string()];
                ++byKind[r.kind.empty() ? "(none)" : r.kind];
            }
            std::printf("assets: %zu\n\nby source:\n", index.records().size());
            for (const auto& [k, v] : bySource) {
                std::printf("  %-20s %zu\n", k.c_str(), v);
            }
            std::printf("\nby kind:\n");
            for (const auto& [k, v] : byKind) {
                std::printf("  %-20s %zu\n", k.c_str(), v);
            }
            std::printf("\nwarnings: %zu\n", index.warnings().size());
            for (const auto& w : index.warnings()) {
                std::printf("  %s\n", w.c_str());
            }
            return 0;
        }
        if (cmd == "find" && argc == 4) {
            const auto k = parseKuidLoose(argv[3]);
            if (!k) {
                std::cerr << "not a KUID: " << argv[3] << "\n";
                return 2;
            }
            const auto found = index.find(*k);
            for (const auto* r : found) {
                printRecord(*r);
            }
            for (const auto* r : index.obsoletedBy(*k)) {
                std::printf("  (listed as obsolete by %s)\n", formatKuid(r->kuid).c_str());
            }
            if (found.empty()) {
                std::cerr << "no asset with " << formatKuid(*k) << " in " << argv[2];
                const auto others = index.allVersions(k->user, k->content);
                if (!others.empty()) {
                    std::cerr << " (other versions exist: try asset-tool versions)";
                }
                std::cerr << "\n";
                return 1;
            }
            return 0;
        }
        if (cmd == "versions" && argc == 4) {
            const auto k = parseKuidLoose(argv[3]);
            if (!k) {
                std::cerr << "expected USER:CONTENT, got " << argv[3] << "\n";
                return 2;
            }
            const auto found = index.allVersions(k->user, k->content);
            for (const auto* r : found) {
                printRecord(*r);
            }
            return found.empty() ? 1 : 0;
        }
        if (cmd == "list" && (argc == 3 || argc == 4)) {
            for (const auto& r : index.records()) {
                if (argc == 4 && r.kind != argv[3]) {
                    continue;
                }
                printRecord(r);
            }
            return 0;
        }
        return usage();
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
}
