// Objects of one route whose model floats: lowest point of the placed model well above the object.
#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "view/AssetMeshes.h"
#include "view/RouteCatalog.h"
#include "view/RouteLoader.h"
#include <cstdio>
#include <map>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    trainz::view::AssetMeshes meshes(idx);
    for (const auto& r : trainz::view::listRoutes(idx)) {
        if (r.route.name.find(argv[2]) == std::string::npos) continue;
        JaArchive a = JaArchive::open(r.route.where.source);
        auto ex = trainz::view::loadRouteExtras(a, r.route.where.folder, "", "");
        std::map<std::string, std::pair<int, float>> hi;
        for (const auto& o : ex.objects) {
            const auto* m = meshes.get(o.kuid); if (!m || m->triangles.empty()) continue;
            float lo = 1e9, top = -1e9, minx = 1e9, maxx = -1e9;
            for (auto& t : m->triangles) for (auto& p : t.p) { lo = std::min(lo, p[2]); top = std::max(top, p[2]); minx = std::min(minx, p[0]); maxx = std::max(maxx, p[0]); }
            if (o.z > 8) { auto rs = idx.find(o.kuid); std::printf("HIGH %s %s at %.0f %.0f z %.1f model %.1f..%.1f span %.1f tris %zu kind %s%s\n", formatKuid(o.kuid).c_str(), o.tag.c_str(), o.worldX(), o.worldY(), o.z, lo, top, maxx - minx, m->triangles.size(), rs.empty() ? "?" : rs.front()->kind.c_str(), m->placeholder ? " placeholder" : ""); }
            if (lo > 5) { auto& e = hi[formatKuid(o.kuid) + " " + o.tag + (m->placeholder ? " placeholder" : "") + (m->billboard ? " billboard" : "")]; e.first++; e.second = lo;
                if (e.first == 1) std::printf("%s at %.0f %.0f z %.1f: model z %.1f..%.1f, x span %.1f, %zu triangles\n", formatKuid(o.kuid).c_str(), o.worldX(), o.worldY(), o.z, lo, top, maxx - minx, m->triangles.size()); }
        }
        for (auto& [k, v] : hi) std::printf("%4d  %s (lowest %.1f m)\n", v.first, k.c_str(), v.second);
    }
}
