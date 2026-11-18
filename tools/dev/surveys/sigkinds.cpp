#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/ObjectContainer.h"
#include "content/ContentError.h"
#include <cstdio>
#include <map>
#include <memory>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch; std::map<std::string, int> n;
    for (const auto& r : idx.records()) {
        if (r.where.container != AssetLocation::Container::Archive) continue;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const JaEntry* oe = a->find(r.where.folder + "/mapfile.obs"); if (!oe) continue;
        try { for (auto& c : parseRouteObjectFile(a->read(*oe)).containers) for (auto& it : c.items) if (it.tag == argv[2]) n[formatKuid(it.kuid)]++; } catch (const ContentError&) {}
    }
    std::multimap<int, std::string> by; for (auto& [k, v] : n) by.emplace(-v, k);
    int shown = 0; for (auto& [v, k] : by) { if (shown++ > 25) break; auto rs = idx.find(*parseKuidLoose(k)); std::printf("%6d %s %s\n", -v, k.c_str(), rs.empty() ? "(not in index)" : rs.front()->kind.c_str()); }
    std::printf("%zu different assets\n", n.size());
}
