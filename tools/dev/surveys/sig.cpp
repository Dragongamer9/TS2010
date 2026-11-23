// sgOb tail (last 23 bytes): piece + distance along it; does it land at the signal?
#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/ObjectContainer.h"
#include "content/PlacedObject.h"
#include "content/TrackLayout.h"
#include "content/ContentError.h"
#include "sim/TrackNetwork.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
using namespace trainz::content;
template <class T> T rd(const std::vector<std::uint8_t>& b, std::size_t o) { T v; std::memcpy(&v, &b[o], sizeof v); return v; }
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    const std::string want = argc > 2 ? argv[2] : "sgOb"; const std::size_t TL = argc > 3 ? std::atoi(argv[3]) : 23;
    std::map<std::string, std::unique_ptr<JaArchive>> arch; std::map<std::string, int> h; int dump = 0; std::map<std::string, std::pair<int,int>> perRoute;
    for (const auto& r : idx.records()) {
        if (r.where.container != AssetLocation::Container::Archive) continue;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const std::string base = r.where.folder + "/";
        const JaEntry* oe = a->find(base + "mapfile.obs"); const JaEntry* te = a->find(base + "mapfile.trk");
        if (!oe || !te) continue;
        TrackLayout L; RouteObjectFile obs;
        try { L = readTrackLayout(parseRouteObjectFile(a->read(*te))); obs = parseRouteObjectFile(a->read(*oe)); } catch (const ContentError&) { continue; }
        trainz::sim::TrackNetwork net(L);
        for (const auto& c : obs.containers) for (const auto& it : c.items) {
            if (it.tag != want) continue;
            auto p = decodePlacedObject(it); if (!p || p->rest.size() < TL + 12) { h["short"]++; continue; }
            const auto& R = p->rest; const std::size_t t = R.size() - TL;
            const std::size_t po = TL == 23 ? 9 : 17;
            std::int32_t piece = rd<std::int32_t>(R, t + po); float s = rd<float>(R, t + po + 4); std::uint8_t d = R[t + po + 8];
            char k[96]; std::snprintf(k, 96, "head %02x%02x%02x%02x %02x%02x%02x%02x %02x", R[t], R[t+1], R[t+2], R[t+3], R[t+4], R[t+5], R[t+6], R[t+7], R[t+8]); h[k]++;
            std::snprintf(k, 96, "dir byte %d", d); h[k]++;
            std::snprintf(k, 96, "last4 %02x%02x%02x%02x", R[t+po+9], R[t+po+10], R[t+po+11], R[t+po+12]); h[k]++;
            auto len = net.length(piece); if (!len) { h["piece not in network"]++; continue; }
            if (!(s >= -0.01 && s <= *len + 0.01)) { h["distance outside piece"]++; continue; }
            auto pt = net.point({piece, s, +1}); auto pt2 = net.point({piece, *len - s, +1});
            double e1 = std::hypot(pt->x - p->worldX(), pt->y - p->worldY()), e2 = std::hypot(pt2->x - p->worldX(), pt2->y - p->worldY());
            auto bin = [](double e) { return e < 1 ? "<1m" : e < 3 ? "1-3m" : e < 6 ? "3-6m" : e < 20 ? "6-20m" : ">=20m"; };
            h[std::string("from-start ") + bin(e1)]++; h[std::string("from-end   ") + bin(e2)]++;
            const double em = d == 1 ? e1 : e2; h[std::string("rule (dir 1: from start, else from end) ") + bin(em)]++;
            if (em < 6) { auto q = d == 1 ? *pt : *pt2; const double hx = std::cos(q.heading), hy = std::sin(q.heading);
                const double side = hx * (p->worldY() - q.y) - hy * (p->worldX() - q.x);
                const double sd = d == 1 ? side : -side;  // relative to the direction the distance runs
                h[std::string("side vs distance direction: ") + (sd > 0.3 ? "left" : sd < -0.3 ? "right" : "on")]++;
                if (sd > 0.3) perRoute[r.where.folder].first++; else if (sd < -0.3) perRoute[r.where.folder].second++;
                char kk[64]; std::snprintf(kk, 64, "|offset| %.1f", std::round(std::fabs(side) * 2) / 2); h[kk]++; }
            // which side of the track is the object (left/right of piece direction from->to), vs dir byte
            if (e1 < 6) { const double hx = std::cos(pt->heading), hy = std::sin(pt->heading);
                const double side = hx * (p->worldY() - pt->y) - hy * (p->worldX() - pt->x);
                h["dir " + std::to_string(d) + (side > 0.3 ? " object left of from->to" : side < -0.3 ? " object right of from->to" : " object on the track")]++; }
            if (dump-- > 0) std::printf("piece %d s=%.2f len=%.1f dir=%d e1=%.2f e2=%.2f\n", piece, s, *len, d, e1, e2);
        }
    }
    for (auto& [k, lr] : perRoute) if (lr.first + lr.second >= 30) std::printf("route %-40s left %5d right %5d (%.0f%% one side)\n", k.c_str(), lr.first, lr.second, 100.0 * std::max(lr.first, lr.second) / (lr.first + lr.second));
    for (auto& [k, n] : h) if (n > 20) std::printf("%6d  %s\n", n, k.c_str());
}
