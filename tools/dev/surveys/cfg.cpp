// Prints an asset's whole config (and the kinds of the sgOb assets of a route if asked).
#include "content/AssetIndex.h"
#include "content/AssetConfig.h"
#include "content/JaArchive.h"
#include <cstdio>
#include <functional>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    auto recs = idx.find(*parseKuidLoose(argv[2]));
    if (recs.empty()) { std::printf("not found\n"); return 1; }
    const auto* r = recs.front();
    JaArchive a = JaArchive::open(r->where.source);
    auto c = parseAssetConfig(a.read(*a.find(r->where.folder + "/" + r->where.configName))).root;
    std::printf("%s kind=%s\n", r->where.folder.c_str(), r->kind.c_str());
    std::function<void(const ConfigNode&, int)> pr = [&](const ConfigNode& n, int d) {
        for (const auto& ch : n.children) {
            if (ch.key == "thumbnails" || ch.key == "string-table" || ch.key == "description") continue;
            std::printf("%*s%s", d * 2, "", ch.key.c_str());
            if (ch.type == ConfigType::Group) { std::printf("\n"); pr(ch, d + 1); continue; }
            for (auto v : ch.floats) std::printf(" %g", v); for (auto v : ch.ints) std::printf(" %d", v);
            if (!ch.text.empty()) std::printf(" '%s'", ch.text.c_str()); if (ch.type == ConfigType::Kuid) std::printf(" <kuid %d:%d>", ch.kuid.user, ch.kuid.content);
            std::printf("\n"); } };
    pr(c, 0);
}
