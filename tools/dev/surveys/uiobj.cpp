#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/ObjectContainer.h"
#include "content/PlacedObject.h"
#include "view/AssetMeshes.h"
#include "content/ContentError.h"
#include <cstdio>
#include <map>
#include <memory>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    trainz::view::AssetMeshes meshes(idx);
    std::map<std::string, std::unique_ptr<JaArchive>> arch; std::map<std::string, int> byTag, ui;
    for (const auto& r : idx.records()) {
        if (r.where.container != AssetLocation::Container::Archive) continue;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const JaEntry* oe = a->find(r.where.folder + "/mapfile.obs"); if (!oe) continue;
        RouteObjectFile f; try { f = parseRouteObjectFile(a->read(*oe)); } catch (const ContentError&) { continue; }
        for (auto& c : f.containers) for (auto& it : c.items) {
            if (it.tag == "cmOb" || it.tag == "mkOb" || it.tag == "tgOb" || it.tag == "tsO2") byTag[it.tag + " " + formatKuid(it.kuid)]++;
            const auto* m = meshes.get(it.kuid);
            if (m && m->meshName.find("ui_") != std::string::npos) ui[it.tag + " " + formatKuid(it.kuid) + " " + m->meshName]++;
        }
    }
    std::printf("-- editor-class objects (top):\n"); std::multimap<int, std::string> s; for (auto& [k, v] : byTag) s.emplace(-v, k); int n = 0; for (auto& [v, k] : s) if (n++ < 15) std::printf("%7d %s\n", -v, k.c_str());
    std::printf("-- objects drawn with a ui_ mesh:\n"); for (auto& [k, v] : ui) std::printf("%7d %s\n", v, k.c_str());
}
