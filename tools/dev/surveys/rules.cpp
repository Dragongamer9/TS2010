#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/SessionFile.h"
#include "content/ContentError.h"
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch; int done = 0;
    for (const auto& r : idx.records()) {
        if (r.kind != "profile" || r.where.container != AssetLocation::Container::Archive || done >= 2) continue;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const JaEntry* pe = a->find(r.where.folder + "/profile.dat"); if (!pe) continue;
        try {
            auto prof = parseSessionProfile(a->readLimited(*pe, std::size_t{256} << 20));
            bool any = false;
            for (const auto& blk : prof.blocks) if (blk.container) for (const auto& it : blk.container->items) if (auto b = decodeBehaviour(it)) {
                if (!any) std::printf("== %s\n", r.where.folder.c_str()); any = true;
                std::printf("  item %d '%s': top keys:", it.index, b->name.c_str());
                int k = 0; for (const auto& c : b->settings.root.children) { if (k++ < 6) std::printf(" %s", c.key.c_str()); }
                std::printf(" (%zu)\n", b->settings.root.children.size());
            }
            done += any;
        } catch (const ContentError&) {}
    }
}
