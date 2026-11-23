// Session .obs junctions vs the route's: which tail bytes differ.
#include "content/AssetIndex.h"
#include "content/AssetConfig.h"
#include "content/JaArchive.h"
#include "content/ObjectContainer.h"
#include "content/PlacedObject.h"
#include "content/ContentError.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch;
    auto open = [&](const AssetRecord& r) -> JaArchive& { auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source)); return *a; };
    std::map<std::string, int> h; int dump = 25;
    for (const auto& r : idx.records()) {
        if (r.kind != "profile" || r.where.container != AssetLocation::Container::Archive) continue;
        JaArchive& sa = open(r); const std::string base = r.where.folder + "/";
        const JaEntry* ce = sa.find(base + r.where.configName); const JaEntry* so = nullptr;
        for (const auto& e : sa.entries()) if (e.name.rfind(base, 0) == 0 && e.name.size() > 4 && e.name.substr(e.name.size() - 4) == ".obs") { so = &e; break; }
        if (!ce || !so) { h["session without .obs"]++; continue; }
        std::optional<Kuid> map; try { auto root = parseAssetConfig(sa.readLimited(*ce, 16 << 20)).root; if (auto* m = root.child("map-kuid")) map = m->asKuid(); } catch (const ContentError&) { continue; }
        if (!map) continue; auto routes = idx.find(*map); if (routes.empty()) continue;
        const AssetRecord& rr = *routes.front(); JaArchive& ra = open(rr); const JaEntry* ro = ra.find(rr.where.folder + "/mapfile.obs"); if (!ro) continue;
        RouteObjectFile S, R;
        try { S = parseRouteObjectFile(sa.read(*so)); R = parseRouteObjectFile(ra.read(*ro)); } catch (const ContentError&) { h["parse fail"]++; continue; }
        std::map<std::int32_t, const ObjectItem*> rj;
        for (auto& c : R.containers) for (auto& it : c.items) if (it.tag == "jnOb") rj[it.index] = &it;
        int sessJ = 0;
        for (auto& c : S.containers) for (auto& it : c.items) {
            if (it.tag != "jnOb") continue; ++sessJ;
            auto f = rj.find(it.index); if (f == rj.end()) { h["session jnOb index not in route"]++; continue; }
            const auto& a = f->second->payload; const auto& b = it.payload;
            if (a.size() != b.size()) { h["size differs"]++; continue; }
            if (a == b) { h["identical"]++; continue; }
            h["differs"]++;
            for (std::size_t k = 0; k < a.size(); ++k) if (a[k] != b[k]) h["diff at -" + std::to_string(a.size() - k) + " from end"]++;
            if (dump-- > 0) { std::printf("%s #%d:\n  route:", r.where.folder.c_str(), it.index); for (std::size_t k = a.size() - 31; k < a.size(); ++k) std::printf(" %02x", a[k]);
                std::printf("\n  sess: "); for (std::size_t k = b.size() - 31; k < b.size(); ++k) std::printf(" %02x", b[k]); std::printf("\n"); }
        }
        h[sessJ ? "sessions with jnOb" : "sessions without jnOb"]++;
    }
    for (auto& [k, n] : h) std::printf("%6d  %s\n", n, k.c_str());
}
