#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "view/AssetMeshes.h"
#include "view/RouteLoader.h"
#include <cstdio>
#include <map>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    trainz::view::AssetMeshes meshes(idx);
    JaArchive a = JaArchive::open(argv[2]);
    auto ex = trainz::view::loadRouteExtras(a, argv[3], "", "");
    std::map<std::string, int> n;
    for (auto& o : ex.objects) { auto* m = meshes.get(o.kuid); if (m) n[formatKuid(o.kuid) + (m->surveyorOnly ? " surveyor-only" : "")]++; }
    for (auto& [k, v] : n) std::printf("%5d %s\n", v, k.c_str());
}
