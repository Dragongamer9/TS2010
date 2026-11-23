// jnOb tail (last 31 bytes) vs. geometry of the nearest 3-link vertex.
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
template <class T> T rd(const std::vector<std::uint8_t>& b, std::size_t o) { T v; std::memcpy(&v, &b[o], sizeof v); return v; }
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch;
    std::map<std::string, int> h; int dump = 10;
    for (const auto& r : idx.records()) {
        if (r.where.container != AssetLocation::Container::Archive) continue;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const std::string base = r.where.folder + "/";
        const JaEntry* oe = a->find(base + "mapfile.obs"); const JaEntry* te = a->find(base + "mapfile.trk");
        if (!oe || !te) continue;
        TrackLayout L; RouteObjectFile obs;
        try { L = readTrackLayout(parseRouteObjectFile(a->read(*te))); obs = parseRouteObjectFile(a->read(*oe)); } catch (const ContentError&) { continue; }
        // vertices by piece
        for (const auto& c : obs.containers) for (const auto& it : c.items) {
            if (it.tag != "jnOb") continue;
            auto p = decodePlacedObject(it); if (!p || p->rest.size() < 31 + 12) { h["short"]++; continue; }
            const auto& R = p->rest; const std::size_t t = R.size() - 31;
            if (rd<std::uint32_t>(R, t) != 2 || R[t + 4] != 1) { h["tail does not start 2,1"]++; continue; }
            const std::uint32_t n = rd<std::uint32_t>(R, t - 0) ; (void)n;
            // name: u32 len right before? check
            bool nameOk = false; for (std::size_t o = 8; o + 4 <= t; ++o) { auto len = rd<std::uint32_t>(R, o); if (o + 4 + len == t) { nameOk = true; break; } }
            h[nameOk ? "name u32-len before tail" : "no name length found"]++;
            float f1 = rd<float>(R, t + 5), f2 = rd<float>(R, t + 9); std::uint32_t z = rd<std::uint32_t>(R, t + 13);
            std::int32_t piece = rd<std::int32_t>(R, t + 17); float f3 = rd<float>(R, t + 21); std::uint8_t b = R[t + 25];
            char k[96]; std::snprintf(k, 96, "f1=%g f2=%g z=%u", f1, f2, z); h[k]++;
            std::snprintf(k, 96, "last5 %02x%02x%02x%02x%02x b=%d", R[t+26], R[t+27], R[t+28], R[t+29], R[t+30], b); h[k]++;
            const TrackStretch* s = L.findStretch(piece);
            if (!s) { h["piece not a stretch"]++; continue; }
            // which end is a 3-link vertex
            const TrackVertex* jv = nullptr; for (int e : {s->from, s->to}) { auto vi = L.vertices.find(e); if (vi != L.vertices.end() && vi->second.links.size() >= 3) jv = &vi->second; }
            if (!jv) { h["piece has no 3+-link end"]++; continue; }
            if (jv->links.size() != 3) { h["4-link"]++; continue; }
            double d = std::hypot(jv->worldX() - p->worldX(), jv->worldY() - p->worldY());
            // headings leaving the vertex
            double hd[3]; std::int32_t pc[3];
            for (int i = 0; i < 3; ++i) { pc[i] = jv->links[i].item; const TrackStretch* q = L.findStretch(pc[i]); auto cv = q ? trackCurve(L, *q) : std::nullopt;
                if (!cv) { hd[i] = NAN; continue; }
                double pt[3]; bool atFrom = q->from == jv->index; cv->point(atFrom ? 0.05 : 0.95, pt); hd[i] = std::atan2(pt[1] - jv->worldY(), pt[0] - jv->worldX()); }
            auto ang = [](double x, double y) { double v = std::fabs(std::remainder(x - y, 2 * M_PI)); return v; };
            int trunk = -1; for (int i = 0; i < 3; ++i) { int j = (i + 1) % 3, l = (i + 2) % 3; if (ang(hd[i], hd[j]) > M_PI / 2 && ang(hd[i], hd[l]) > M_PI / 2) trunk = i; }
            if (trunk < 0) { h["no geometric trunk"]++; continue; }
            int j = (trunk + 1) % 3, l = (trunk + 2) % 3;
            // straighter leg: closest to opposite of trunk
            double dj = M_PI - ang(hd[trunk], hd[j]), dl = M_PI - ang(hd[trunk], hd[l]);
            int straight = dj <= dl ? j : l, div = dj <= dl ? l : j;
            // left/right of the diverging leg, seen from the trunk looking along the legs
            double fwd = hd[trunk] + M_PI; double side = std::sin(hd[div] - fwd);
            int idx3 = -1; for (int i = 0; i < 3; ++i) if (pc[i] == piece) idx3 = i;
            std::string role = idx3 == trunk ? "trunk" : idx3 == straight ? "straighter leg" : idx3 == div ? "diverging leg" : "?";
            // stored piece's leg side when a leg
            std::string sd = idx3 == trunk ? "" : (std::sin(hd[idx3] - fwd) > 0 ? " (left of other leg?)" : "");
            h["piece is " + role + ", b=" + std::to_string(b)]++;
            h[std::string("trunk link flag lone: ") + ((jv->links[trunk].flag != jv->links[j].flag && jv->links[j].flag == jv->links[l].flag) ? "yes" : "no")]++;
            h[std::string("lever ") + (d < 1 ? "<1m" : d < 3 ? "1-3m" : d < 10 ? "3-10m" : ">=10m")]++;
            h[std::string("f3 ") + (f3 == 0 ? "0" : "nonzero")]++;
            if (dump-- > 0) std::printf("piece %d role %s b=%d f3=%g d=%.1f div side %.2f\n", piece, role.c_str(), b, f3, d, side);
        }
    }
    for (auto& [k, n] : h) std::printf("%6d  %s\n", n, k.c_str());
}
