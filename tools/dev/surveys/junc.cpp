// jnOb survey: payload bytes after the base block, nearest 3+-link vertex, and whether any
// i32 in the payload equals that vertex's index or one of its pieces.
#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/ObjectContainer.h"
#include "content/PlacedObject.h"
#include "content/TrackLayout.h"
#include "content/ContentError.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    const std::string want = argc > 2 ? argv[2] : "jnOb";
    int dumps = argc > 3 ? std::atoi(argv[3]) : 12;
    std::map<std::string, std::unique_ptr<JaArchive>> arch;
    std::map<std::size_t, int> sizes; std::map<std::string, int> hits; int total = 0, near1 = 0, near5 = 0;
    std::map<int, std::map<std::string, int>> byteVals;
    for (const auto& r : idx.records()) {
        if (r.where.container != AssetLocation::Container::Archive) continue;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const std::string base = r.where.folder + "/";
        const JaEntry* oe = a->find(base + "mapfile.obs"); const JaEntry* te = a->find(base + "mapfile.trk");
        if (!oe || !te) continue;
        TrackLayout L; RouteObjectFile obs;
        try { L = readTrackLayout(parseRouteObjectFile(a->read(*te))); obs = parseRouteObjectFile(a->read(*oe)); } catch (const ContentError&) { continue; }
        std::vector<const TrackVertex*> junc; for (auto& [i, v] : L.vertices) if (v.links.size() >= 3) junc.push_back(&v);
        for (const auto& c : obs.containers) for (const auto& it : c.items) {
            if (it.tag != want) continue;
            auto p = decodePlacedObject(it); if (!p) { hits["undecoded"]++; continue; }
            ++total; sizes[p->rest.size()]++;
            const TrackVertex* best = nullptr; double bd = 1e18;
            for (auto* v : junc) { double d = std::hypot(v->worldX() - p->worldX(), v->worldY() - p->worldY()); if (d < bd) { bd = d; best = v; } }
            near1 += bd <= 1; near5 += bd <= 5;
            for (std::size_t k = 0; k < std::min<std::size_t>(p->rest.size(), 24); ++k) byteVals[int(k)][std::to_string(p->rest[k])]++;
            if (best) for (std::size_t o = 0; o + 4 <= p->rest.size(); ++o) { std::int32_t w; std::memcpy(&w, &p->rest[o], 4);
                if (w == best->index) hits["vertex@" + std::to_string(o)]++;
                for (auto& l : best->links) if (w == l.item) hits["piece@" + std::to_string(o)]++; }
            if (p->rest.size() >= 12) { std::uint32_t n; std::memcpy(&n, &p->rest[8], 4);
                if (n < 200 && 12 + n + 4 <= p->rest.size()) {
                    std::vector<std::uint8_t> R(p->rest.begin() + 12 + n, p->rest.end());
                    hits["tail size " + std::to_string(R.size())]++;
                    if (R.size() >= 30) { std::int32_t piece; float f2; std::memcpy(&piece, &R[17], 4); std::memcpy(&f2, &R[21], 4);
                        std::string role = "not a link";
                        if (best) { int f = -1, same = 0; for (auto& l : best->links) if (l.item == piece) f = l.flag;
                            for (auto& l : best->links) same += (l.flag == f); if (f >= 0) role = same == 1 ? "trunk (lone flag)" : "leg (shared flag)"; }
                        hits["piece role (3 links=" + std::to_string(best ? best->links.size() : 0) + "): " + role + ", byte25=" + std::to_string(R[25])]++;
                        hits["R[4]=" + std::to_string(R[4]) + " R0=" + std::to_string(R[0])]++;
                        char b2[64]; std::snprintf(b2, 64, "f2 %s", f2 == 0 ? "0" : f2 < 30 ? "<30" : ">=30"); hits[b2]++;
                    }
                } else { if (hits["name len odd"]++ < 6) { std::printf("ODD %s #%d (%zu):", r.where.folder.c_str(), it.index, p->rest.size()); for (std::size_t k = 0; k < p->rest.size() && k < 80; ++k) std::printf(" %02x", p->rest[k]); std::printf("\n"); } } }
            if (p->rest.size() == (std::size_t)std::atoi(getenv("SZ") ? getenv("SZ") : "0") && dumps-- > 0) { std::printf("%s #%d d=%.2f vtx %d links", r.where.folder.c_str(), it.index, bd, best ? best->index : -1);
                if (best) for (auto& l : best->links) std::printf(" %d/%d", l.item, l.flag);
                std::printf("\n  rest(%zu):", p->rest.size()); for (std::size_t k = 0; k < p->rest.size() && k < 96; ++k) std::printf(" %02x", p->rest[k]); std::printf("\n"); }
        }
    }
    std::printf("%s: %d; nearest 3+-link vertex within 1 m %d, within 5 m %d\n", want.c_str(), total, near1, near5);
    for (auto& [s, n] : sizes) std::printf("  rest size %zu: %d\n", s, n);
    for (auto& [h, n] : hits) std::printf("  %s: %d\n", h.c_str(), n);
    for (auto& [k, m] : byteVals) { if (m.size() > 6) { std::printf("  byte %d: %zu values\n", k, m.size()); continue; } std::printf("  byte %d:", k); for (auto& [v, n] : m) std::printf(" %s×%d", v.c_str(), n); std::printf("\n"); }
}
