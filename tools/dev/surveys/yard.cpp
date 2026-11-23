// The track around given pieces of one session's route: pieces (length, end vertices) and the
// vertices' links, a few hops out, plus which pieces the session's trains stand on.
// Usage: yard INSTALL "SESSION NAME PART" DEPTH PIECE...
#include "content/AssetIndex.h"
#include "content/AssetConfig.h"
#include "content/JaArchive.h"
#include "content/ObjectContainer.h"
#include "content/SessionFile.h"
#include "content/TrackLayout.h"
#include "content/ContentError.h"
#include "sim/TrackNetwork.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
using namespace trainz::content;
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch;
    auto open = [&](const AssetRecord& r) -> JaArchive& { auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source)); return *a; };
    const std::string want = argv[2];
    const int depth = std::atoi(argv[3]);
    for (const auto& r : idx.records()) {
        if (r.kind != "profile" || r.where.container != AssetLocation::Container::Archive) continue;
        JaArchive& sa = open(r); const std::string base = r.where.folder + "/";
        const JaEntry* ce = sa.find(base + r.where.configName); const JaEntry* pe = sa.find(base + "profile.dat");
        if (!ce || !pe) continue;
        std::optional<Kuid> map; std::string name;
        try { auto root = parseAssetConfig(sa.readLimited(*ce, 16 << 20)).root; if (auto* m = root.child("map-kuid")) map = m->asKuid(); if (auto* u = root.child("username")) name = u->text; } catch (const ContentError&) { continue; }
        if (!map || name.find(want) == std::string::npos) continue;
        auto routes = idx.find(*map); if (routes.empty()) continue;
        const AssetRecord& rr = *routes.front(); JaArchive& ra = open(rr); const JaEntry* te = ra.find(rr.where.folder + "/mapfile.trk"); if (!te) continue;
        std::vector<Train> ts;
        for (const auto& blk : parseSessionProfile(sa.readLimited(*pe, std::size_t{256} << 20)).blocks) if (blk.container) for (const auto& it : blk.container->items) if (auto t = decodeTrain(it)) ts.push_back(*t);
        TrackLayout L = readTrackLayout(parseRouteObjectFile(ra.read(*te)));
        trainz::sim::TrackNetwork net(L);
        TrackLocator loc(L, 0, 0, 1e9);
        std::map<std::int32_t, std::string> standing;
        for (std::size_t i = 0; i < ts.size(); ++i) for (const auto& v : ts[i].vehicles) { auto s = loc.nearest(v.boardX * kBaseboardSize + v.x, v.boardY * kBaseboardSize + v.y, 2.0); if (s) standing[s->piece] += " t" + std::to_string(i + 1) + ":" + v.name; }
        std::printf("session '%s'\n", name.c_str());
        std::set<std::int32_t> pieces, seen;
        for (int i = 4; i < argc; ++i) pieces.insert(std::atoi(argv[i]));
        std::set<std::int32_t> frontier = pieces;
        for (int d = 0; d < depth; ++d) {
            std::set<std::int32_t> next;
            for (auto p : frontier) {
                const TrackStretch* s = L.findStretch(p); if (!s) continue;
                for (auto v : {s->from, s->to}) { auto it = L.vertices.find(v); if (it == L.vertices.end()) continue; for (auto& l : it->second.links) if (!pieces.count(l.item)) next.insert(l.item); }
            }
            for (auto p : next) pieces.insert(p);
            frontier = next;
        }
        std::set<std::int32_t> verts;
        for (auto p : pieces) {
            const TrackStretch* s = L.findStretch(p); if (!s) continue;
            const auto len = net.length(p);
            std::printf("piece %6d  %4.0f m  %d -> %d%s\n", p, len ? *len : -1.0, s->from, s->to, standing.count(p) ? standing[p].c_str() : "");
            verts.insert(s->from); verts.insert(s->to);
        }
        for (auto v : verts) {
            auto it = L.vertices.find(v); if (it == L.vertices.end()) continue;
            if (it->second.links.size() < 3) continue;
            std::printf("junction %6d at (%.0f, %.0f) last %d:", v, it->second.worldX(), it->second.worldY(), it->second.lastByte);
            for (auto& l : it->second.links) std::printf(" %d/%d", l.item, l.flag);
            std::printf("\n");
        }
        for (auto v : verts) { auto it = L.vertices.find(v); if (it != L.vertices.end() && it->second.links.size() == 1) std::printf("line end %d\n", v); }
        break;
    }
}
