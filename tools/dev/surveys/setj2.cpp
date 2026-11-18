// Prints the settings of "Set Junctions" rules in the disc's sessions.
#include "content/AssetIndex.h"
#include "content/AssetConfig.h"
#include "content/JaArchive.h"
#include "content/SessionFile.h"
#include "content/ContentError.h"
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch; int shown = 0, rules = 0; std::map<int,int> hist;
    std::function<void(const ConfigNode&, int)> pr = [&](const ConfigNode& n, int d) {
        for (const auto& ch : n.children) { if (d > 6) return; std::printf("%*s%s", 2 + d * 2, "", ch.key.c_str());
            if (ch.type == ConfigType::Group) { std::printf("\n"); pr(ch, d + 1); continue; }
            for (auto v : ch.ints) std::printf(" %d", v); for (auto v : ch.floats) std::printf(" %g", v); if (!ch.text.empty()) std::printf(" '%s'", ch.text.c_str());
            if (ch.type == ConfigType::Kuid) std::printf(" <kuid %d:%d>", ch.kuid.user, ch.kuid.content); std::printf("\n"); } };
    for (const auto& r : idx.records()) {
        if (r.kind != "profile" || r.where.container != AssetLocation::Container::Archive) continue;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const JaEntry* pe = a->find(r.where.folder + "/profile.dat"); if (!pe) continue;
        try {
            for (const auto& blk : parseSessionProfile(a->readLimited(*pe, std::size_t{256} << 20)).blocks) if (blk.container)
                for (const auto& it : blk.container->items) if (auto b = decodeBehaviour(it)) if ((b->name == "Driver Setup" || b->name == "Driver Command")) {
                    ++rules; if (shown++ < 2) { std::printf("== %s: %s\n", r.where.folder.c_str(), b->name.c_str()); pr(b->settings.root, 0); } }
        } catch (const ContentError&) {}
    }
    std::printf("%d junction rules\n", rules); for (auto& [k, v] : hist) std::printf("  setting %d: %d junction entries\n", k, v); (void)shown;
}
