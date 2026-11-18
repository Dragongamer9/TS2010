#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/ObjectContainer.h"
#include "content/TrackLayout.h"
#include "content/ContentError.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch; std::map<std::string, int> h;
    for (const auto& r : idx.records()) {
        if (r.where.container != AssetLocation::Container::Archive) continue;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const JaEntry* te = a->find(r.where.folder + "/mapfile.trk"); if (!te) continue;
        TrackLayout L; try { L = readTrackLayout(parseRouteObjectFile(a->read(*te))); } catch (const ContentError&) { continue; }
        for (auto& [i, v] : L.vertices) {
            h["links " + std::to_string(v.links.size()) + " lastByte " + std::to_string(v.lastByte)]++;
            if (v.links.size() == 3) {
                // which slot is the trunk (lone flag)
                int t = -1; for (int k = 0; k < 3; ++k) { int same = 0; for (auto& l : v.links) same += l.flag == v.links[k].flag; if (same == 1) t = k; }
                h["3-link trunk slot " + std::to_string(t) + " lastByte " + std::to_string(v.lastByte)]++;
                if (t >= 0) { std::vector<int> legs; for (int k = 0; k < 3; ++k) if (k != t) legs.push_back(k);
                    auto mid = [&](int k, double out[3]) { const TrackStretch* q = L.findStretch(v.links[k].item); auto c = q ? trackCurve(L, *q) : std::nullopt; if (!c) return false; c->point(0.5, out); return true; };
                    double pt[3], p0[3], p1[3];
                    if (mid(t, pt) && mid(legs[0], p0) && mid(legs[1], p1)) {
                        const double fx = v.worldX() - pt[0], fy = v.worldY() - pt[1];
                        auto cross = [&](double* p) { return fx * (p[1] - v.worldY()) - fy * (p[0] - v.worldX()); };
                        const double c0 = cross(p0), c1 = cross(p1);
                        h[std::string("first leg slot is ") + (c0 > c1 ? "left" : c0 < c1 ? "right" : "tie")]++;
                    } }
            }
        }
    }
    for (auto& [k, n] : h) std::printf("%7d  %s\n", n, k.c_str());
}
