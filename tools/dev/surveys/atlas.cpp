#include "content/JaArchive.h"
#include "content/TextureFile.h"
#include "content/ContentError.h"
#include "view/AssetMeshes.h"
#include <cstdio>
#include <map>
using namespace trainz::content;
int main(int argc, char** argv) {
    std::map<std::string, int> fmt, kind; int shown = 0;
    for (int i = 1; i < argc; ++i) { JaArchive a = JaArchive::open(argv[i]);
        for (const auto& e : a.entries()) { if (!e.name.ends_with("billboardatlas_diffuse.texture")) continue;
            try { auto tex = parseTexture(a.readLimited(e, 64 << 20)); const MipLevel* pick = &tex.mips.back(); for (auto& m : tex.mips) if (m.width <= 512) { pick = &m; break; }
                trainz::view::SceneTexture st; st.width = pick->width; st.height = pick->height; st.rgba = decodeToRgba8(*pick, tex.info.format);
                std::size_t solid = 0; for (std::size_t q = 3; q < st.rgba.size(); q += 4) solid += st.rgba[q] >= 128;
                const double frac = double(solid) / (st.width * st.height);
                auto c = trainz::view::firstAtlasCell(st);
                const double cellFrac = c ? double((c->x1 - c->x0) * (c->y1 - c->y0)) / (st.width * st.height) : 0;
                fmt[std::to_string(static_cast<int>(tex.info.format))]++;
                const char* k = frac > 0.95 ? "solid (no cut-out alpha)" : cellFrac > 0.5 ? "cell over half the atlas" : "ok";
                kind[k]++;
                if (std::string(k) != "ok" && shown++ < 5) std::printf("%s fmt %d solid %.2f cell %.2f\n", e.name.c_str(), static_cast<int>(tex.info.format), frac, cellFrac);
            } catch (const ContentError&) { kind["unreadable"]++; } } }
    for (auto& [k, v] : fmt) std::printf("format %s: %d\n", k.c_str(), v);
    for (auto& [k, v] : kind) std::printf("%s: %d\n", k.c_str(), v);
}
