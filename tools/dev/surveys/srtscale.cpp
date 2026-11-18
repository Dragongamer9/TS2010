#include "content/AssetIndex.h"
#include "content/AssetConfig.h"
#include "content/JaArchive.h"
#include "content/ContentError.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch; std::vector<double> h, raw; int noScale = 0; std::map<std::string,int> scaleForms; int otherMeshScale = 0;
    for (const auto& r : idx.records()) {
        if (r.where.container != AssetLocation::Container::Archive) continue;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const std::string base = r.where.folder.empty() ? "" : r.where.folder + "/";
        const JaEntry* ce = a->find(base + r.where.configName); if (!ce) continue;
        ConfigNode root; try { root = parseAssetConfig(a->readLimited(*ce, 16 << 20)).root; } catch (const ContentError&) { continue; }
        const ConfigNode* mt = root.child("mesh-table"); if (!mt) continue;
        for (const auto& e : mt->children) { const ConfigNode* m = e.child("mesh"); if (!m || m->type != ConfigType::String) continue;
            const ConfigNode* sc = e.child("mesh-scale");
            std::string t = m->text; for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!t.ends_with(".srt")) { if (sc) ++otherMeshScale; continue; }
            const JaEntry* se = a->find(base + m->text); if (!se) continue;
            auto d = a->readLimited(*se, std::size_t{64} << 20); if (d.size() < 60) continue; float top; std::memcpy(&top, d.data() + 56, 4);
            double s = 1; if (sc) { scaleForms[std::to_string(static_cast<int>(sc->type)) + " n=" + std::to_string(sc->floats.size() + sc->ints.size())]++;
                if (!sc->floats.empty()) { s = 0; for (float f : sc->floats) s += f; s /= sc->floats.size(); } } else ++noScale;
            raw.push_back(top); h.push_back(top * s); } }
    std::sort(h.begin(), h.end()); std::sort(raw.begin(), raw.end());
    auto q = [](const std::vector<double>& v, double p) { return v.empty() ? 0.0 : v[std::size_t(p * (v.size() - 1))]; };
    std::printf("%zu srt entries (%d without mesh-scale; %d non-srt entries have mesh-scale)\n", h.size(), noScale, otherMeshScale);
    std::printf("raw top z: min %.1f p10 %.1f median %.1f p90 %.1f max %.1f\n", q(raw,0), q(raw,.1), q(raw,.5), q(raw,.9), q(raw,1));
    std::printf("scaled height: min %.1f p10 %.1f median %.1f p90 %.1f max %.1f\n", q(h,0), q(h,.1), q(h,.5), q(h,.9), q(h,1));
    for (auto& [k, v] : scaleForms) std::printf("  mesh-scale type %s: %d\n", k.c_str(), v);
}
