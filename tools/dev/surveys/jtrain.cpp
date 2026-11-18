// Trains standing across a 3-link vertex: which leg they occupy vs the vertex's last byte.
#include "content/AssetIndex.h"
#include "content/AssetConfig.h"
#include "content/JaArchive.h"
#include "content/ObjectContainer.h"
#include "content/SessionFile.h"
#include "content/TrackLayout.h"
#include "content/ContentError.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch;
    auto open = [&](const AssetRecord& r) -> JaArchive& { auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source)); return *a; };
    std::map<std::string, int> h;
    for (const auto& r : idx.records()) {
        if (r.kind != "profile" || r.where.container != AssetLocation::Container::Archive) continue;
        JaArchive& sa = open(r); const std::string base = r.where.folder + "/";
        const JaEntry* ce = sa.find(base + r.where.configName); const JaEntry* pe = sa.find(base + "profile.dat");
        if (!ce || !pe) continue;
        std::optional<Kuid> map; try { auto root = parseAssetConfig(sa.readLimited(*ce, 16 << 20)).root; if (auto* m = root.child("map-kuid")) map = m->asKuid(); } catch (const ContentError&) { continue; }
        if (!map) continue; auto routes = idx.find(*map); if (routes.empty()) continue;
        const AssetRecord& rr = *routes.front(); JaArchive& ra = open(rr); const JaEntry* te = ra.find(rr.where.folder + "/mapfile.trk"); if (!te) continue;
        std::vector<Train> ts;
        try { for (const auto& blk : parseSessionProfile(sa.readLimited(*pe, std::size_t{256} << 20)).blocks) if (blk.container) for (const auto& it : blk.container->items) if (auto t = decodeTrain(it)) ts.push_back(*t); } catch (const ContentError&) { continue; }
        TrackLayout L; try { L = readTrackLayout(parseRouteObjectFile(ra.read(*te))); } catch (const ContentError&) { continue; }
        TrackLocator loc(L, 0, 0, 1e9);
        for (const auto& t : ts) {
            std::set<std::int32_t> on;
            for (const auto& v : t.vehicles) { auto s = loc.nearest(v.boardX * kBaseboardSize + v.x, v.boardY * kBaseboardSize + v.y, 1.0); if (s) on.insert(s->piece); }
            for (auto& [i, vx] : L.vertices) {
                if (vx.links.size() != 3) continue;
                int tr = -1; for (int k = 0; k < 3; ++k) { int same = 0; for (auto& l : vx.links) same += l.flag == vx.links[k].flag; if (same == 1) tr = k; }
                if (tr < 0 || !on.count(vx.links[tr].item)) continue;
                std::vector<int> legs; for (int k = 0; k < 3; ++k) if (k != tr) legs.push_back(k);
                bool o0 = on.count(vx.links[legs[0]].item), o1 = on.count(vx.links[legs[1]].item);
                if (o0 == o1) continue;  // neither or both
                const int legOrder = o0 ? 0 : 1;  // first or second leg slot
                // geometric side: leg's midpoint relative to trunk-forward direction
                auto mid = [&](int k, double out[3]) { const TrackStretch* q = L.findStretch(vx.links[k].item); auto c = q ? trackCurve(L, *q) : std::nullopt; if (!c) return false; c->point(0.5, out); return true; };
                double pt[3], p0[3], p1[3];
                if (!mid(tr, pt) || !mid(legs[0], p0) || !mid(legs[1], p1)) continue;
                const double fx = vx.worldX() - pt[0], fy = vx.worldY() - pt[1];  // forward: from trunk toward legs
                auto cross = [&](double* p) { return fx * (p[1] - vx.worldY()) - fy * (p[0] - vx.worldX()); };
                const double c0 = cross(p0), c1 = cross(p1);
                const bool occLeft = (o0 ? c0 : c1) > (o0 ? c1 : c0);  // the occupied leg is the more-left one
                h["lastByte " + std::to_string(vx.lastByte) + ": occupied leg slot-order " + std::to_string(legOrder)]++;
                h["lastByte " + std::to_string(vx.lastByte) + ": occupied leg is the " + (occLeft ? "left" : "right") + " one"]++;
                h["trunk slot " + std::to_string(tr) + " lastByte " + std::to_string(vx.lastByte) + " order " + std::to_string(legOrder)]++;
            }
        }
    }
    for (auto& [k, n] : h) std::printf("%6d  %s\n", n, k.c_str());
}
