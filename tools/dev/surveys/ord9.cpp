#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "view/RouteCatalog.h"
#include "view/RouteLoader.h"
#include <cstdio>
#include <functional>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::function<void(const ConfigNode&, int)> pr = [&](const ConfigNode& n, int d) {
        for (const auto& ch : n.children) { std::printf("%*s%s [type %d]", 2 + d * 2, "", ch.key.c_str(), static_cast<int>(ch.type));
            if (ch.type == ConfigType::Group) { std::printf("\n"); pr(ch, d + 1); continue; }
            for (auto v : ch.ints) std::printf(" %d", v); for (auto v : ch.floats) std::printf(" %g", v); if (!ch.text.empty()) std::printf(" '%s'", ch.text.c_str());
            if (ch.type == ConfigType::Kuid) std::printf(" <kuid %d:%d>", ch.kuid.user, ch.kuid.content); std::printf("\n"); } };
    for (const auto& rt : trainz::view::listRoutes(idx)) for (const auto& se : rt.sessions) {
        if (se.name != argv[2]) continue;
        JaArchive a = JaArchive::open(rt.route.where.source);
        auto ex = trainz::view::loadRouteExtras(a, rt.route.where.folder, se.where.source.string(), se.where.folder);
        for (const auto& rule : ex.rules) { if (rule.name != "Driver Setup") continue;
            const auto* drivers = rule.settings.root.child("drivers");
            for (const auto& d : drivers->children) { const auto* v = d.child("vehicle"); if (!v || v->text != argv[3]) continue;
                const auto* cmds = d.child("driver-commands"); int k = 0;
                for (const auto& c : cmds->children) { if (k >= std::atoi(argv[4]) && k < std::atoi(argv[4]) + 3) { std::printf("command %d:\n", k); pr(c, 0); } ++k; } } }
    }
}
