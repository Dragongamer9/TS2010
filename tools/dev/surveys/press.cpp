#include "content/AssetIndex.h"
#include "content/AssetConfig.h"
#include "content/JaArchive.h"
#include "content/ContentError.h"
#include <cstdio>
#include <map>
#include <memory>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch; std::map<std::string, std::map<std::string, int>> v;
    int n = 0;
    for (const auto& r : idx.records()) {
        if ((r.kind != "engine" && r.kind != "steam-engine") || r.where.container != AssetLocation::Container::Archive) continue;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const JaEntry* ce = a->find(r.where.folder + "/" + r.where.configName); if (!ce) continue;
        ConfigNode root; try { root = parseAssetConfig(a->read(*ce)).root; } catch (const ContentError&) { continue; }
        const ConfigNode* p = root.child("pressure"); if (!p) { v["(none)"]["no pressure"]++; continue; }
        ++n;
        for (const char* k : {"mainreservoir", "brakepipe", "brakeinitial", "brakefull", "scale"}) {
            const ConfigNode* c = p->child(k); char b[64];
            if (!c) std::snprintf(b, 64, "missing"); else std::snprintf(b, 64, "%.6g", c->floats.empty() ? (c->ints.empty() ? -1.0 : c->ints[0]) : c->floats[0]);
            v[k][b]++;
        }
    }
    std::printf("%d enginespecs with pressure\n", n);
    for (auto& [k, m] : v) { std::printf("%s:", k.c_str()); for (auto& [val, c] : m) std::printf(" %s x%d,", val.c_str(), c); std::printf("\n"); }
}
