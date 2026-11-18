// Old-layout trains on every disc session: distance of each vehicle to its route's modelled
// track; split into trains that were accepted before (all oldWord 0) and the new ones.
#include "content/AssetIndex.h"
#include "content/AssetConfig.h"
#include "content/JaArchive.h"
#include "content/ObjectContainer.h"
#include "content/SessionFile.h"
#include "content/TrackLayout.h"
#include "content/ContentError.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch;
    auto open = [&](const AssetRecord& r) -> JaArchive& { auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source)); return *a; };
    std::map<std::string, std::vector<double>> dist; std::map<std::string, int> trains, wordPos;
    for (const auto& r : idx.records()) {
        if (r.kind != "profile" || r.where.container != AssetLocation::Container::Archive) continue;
        JaArchive& sa = open(r); const std::string base = r.where.folder + "/";
        const JaEntry* ce = sa.find(base + r.where.configName); const JaEntry* pe = sa.find(base + "profile.dat");
        if (!ce || !pe) continue;
        std::optional<Kuid> map; try { auto root = parseAssetConfig(sa.readLimited(*ce, 16 << 20)).root; if (auto* m = root.child("map-kuid")) map = m->asKuid(); } catch (const ContentError&) { continue; }
        if (!map) continue; auto routes = idx.find(*map); if (routes.empty()) continue;
        const AssetRecord& rr = *routes.front(); JaArchive& ra = open(rr); const JaEntry* te = ra.find(rr.where.folder + "/mapfile.trk"); if (!te) continue;
        std::vector<Train> ts;
        try { for (const auto& blk : parseSessionProfile(sa.readLimited(*pe, std::size_t{256} << 20)).blocks) if (blk.container) for (const auto& it : blk.container->items) if (auto t = decodeTrain(it)) if (t->oldLayout) ts.push_back(*t); } catch (const ContentError&) { continue; }
        if (ts.empty()) continue;
        TrackLayout L; try { auto d = ra.read(*te); L = readTrackLayout(parseRouteObjectFile(d)); } catch (const ContentError&) { continue; }
        TrackLocator loc(L, 0, 0, 1e9);
        for (const auto& t : ts) {
            bool anyOne = false; for (const auto& v : t.vehicles) anyOne = anyOne || v.oldWord != 0;
            const std::string group = anyOne ? "new (has oldWord 1)" : "accepted before";
            trains[group]++;
            for (std::size_t k = 0; k < t.vehicles.size(); ++k) {
                const auto& v = t.vehicles[k];
                const auto s = loc.nearest(v.boardX * kBaseboardSize + v.x, v.boardY * kBaseboardSize + v.y, 25.0);
                dist[group].push_back(s ? s->distance : 99.0);
                if (anyOne) wordPos[std::string("oldWord ") + std::to_string(v.oldWord) + (k == 0 ? " first" : k + 1 == t.vehicles.size() ? " last" : " middle")]++;
            }
        }
    }
    for (auto& [g, v] : dist) { std::sort(v.begin(), v.end()); std::size_t w1 = 0, w3 = 0; for (double d : v) { w1 += d <= 1; w3 += d <= 3; }
        std::printf("%-22s trains %4d vehicles %5zu: within 1 m %.1f%%, within 3 m %.1f%%, median %.2f m, worst %.1f m\n", g.c_str(), trains[g], v.size(), 100.0 * w1 / v.size(), 100.0 * w3 / v.size(), v[v.size() / 2], v.back()); }
    for (auto& [k, n] : wordPos) std::printf("  %s: %d\n", k.c_str(), n);
}
