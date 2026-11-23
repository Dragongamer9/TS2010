#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/ObjectContainer.h"
#include "content/PlacedObject.h"
#include <cstdio>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    const auto* r = idx.find(*parseKuidLoose(argv[2])).front(); JaArchive a = JaArchive::open(r->where.source);
    int shown = 0;
    for (auto& c : parseRouteObjectFile(a.read(*a.find(r->where.folder + "/mapfile.obs"))).containers) for (auto& it : c.items) {
        if (it.tag != "tsO2") continue; auto p = decodePlacedObject(it); if (!p) continue;
        if (trackAttachment(*p)) continue;
        if (shown++ > 6) break;
        std::printf("%s (%zu):", formatKuid(it.kuid).c_str(), p->rest.size()); for (std::size_t k = 0; k < p->rest.size(); ++k) { unsigned char ch = p->rest[k]; if (ch >= 32 && ch < 127) std::printf("%c", ch); else std::printf("."); } std::printf("\n   ");
        for (std::size_t k = p->rest.size() > 32 ? p->rest.size() - 32 : 0; k < p->rest.size(); ++k) std::printf(" %02x", p->rest[k]); std::printf("\n");
    }
}
