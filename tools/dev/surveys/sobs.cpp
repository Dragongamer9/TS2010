#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/ObjectContainer.h"
#include "content/PlacedObject.h"
#include <cstdio>
#include <map>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    const auto* r = idx.find(*parseKuidLoose(argv[2])).front(); JaArchive a = JaArchive::open(r->where.source);
    for (const auto& e : a.entries()) if (e.name.rfind(r->where.folder + "/", 0) == 0 && e.name.size() > 4 && e.name.substr(e.name.size() - 4) == ".obs") {
        std::map<std::string, int> tags; int named = 0;
        for (auto& c : parseRouteObjectFile(a.read(e)).containers) for (auto& it : c.items) { tags[it.tag]++; if (it.tag == "tsO2") { auto p = decodePlacedObject(it); if (!p) { std::printf(" undecoded tsO2\n"); continue; } auto t = trackAttachment(*p); std::printf(" tsO2 %s rest %zu attach %s:", formatKuid(it.kuid).c_str(), p->rest.size(), t ? "yes" : "no"); for (std::size_t k = 0; k < p->rest.size() && k < 40; ++k) std::printf(" %02x", p->rest[k]); std::printf("\n"); ++named; } }
        std::printf("%s:", e.name.c_str()); for (auto& [t, n] : tags) std::printf(" %s x%d", t.c_str(), n); std::printf("\n");
    }
}
