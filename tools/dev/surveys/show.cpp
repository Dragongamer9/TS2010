#include "content/AssetIndex.h"
#include "content/AssetConfig.h"
#include "content/JaArchive.h"
#include <cstdio>
#include <functional>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    const auto* r = idx.find(*parseKuidLoose(argv[2])).front();
    JaArchive a = JaArchive::open(r->where.source);
    auto c = parseAssetConfig(a.read(*a.find(r->where.folder + "/" + r->where.configName))).root;
    std::function<void(const ConfigNode&, int)> pr = [&](const ConfigNode& n, int d) {
        for (const auto& ch : n.children) {
            if (ch.key == "motor" || ch.key == "throttle-power" || ch.key == "dynamic-brake" || d > 0) {
                std::printf("%*s%s", d * 2, "", ch.key.c_str());
                if (ch.type == ConfigType::Group) { std::printf("\n"); pr(ch, d + 1); continue; }
                for (auto v : ch.floats) std::printf(" %g", v); for (auto v : ch.ints) std::printf(" %d", v); if (!ch.text.empty()) std::printf(" '%s'", ch.text.c_str());
                std::printf("\n"); } } };
    pr(c, 0);
}
