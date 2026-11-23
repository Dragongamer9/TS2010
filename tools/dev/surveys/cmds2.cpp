#include "content/AssetIndex.h"
#include "content/AssetConfig.h"
#include "content/JaArchive.h"
#include "content/SessionFile.h"
#include "content/ContentError.h"
#include "game/RouteDrive.h"
#include <cstdio>
#include <map>
#include <memory>
#include <set>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    trainz::view::AssetMeshes meshes(idx);
    std::map<std::string, std::unique_ptr<JaArchive>> arch;
    std::map<std::string, int> count; std::map<std::string, std::set<std::string>> props; std::map<std::string, std::string> example; std::map<std::string, int> folders;
    for (const auto& r : idx.records()) {
        if (r.kind != "profile" || r.where.container != AssetLocation::Container::Archive) continue;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const JaEntry* pe = a->find(r.where.folder + "/profile.dat"); if (!pe) continue;
        std::vector<Behaviour> rules;
        try { for (const auto& blk : parseSessionProfile(a->readLimited(*pe, std::size_t{256} << 20)).blocks) if (blk.container) for (const auto& it : blk.container->items) if (auto b = decodeBehaviour(it)) rules.push_back(*b); } catch (const ContentError&) { continue; }
        for (const auto& o : trainz::game::driverOrders(rules)) for (const auto& c : o.commands) {
            std::string name = formatKuid(c.command);
            if (auto cfg = meshes.config(c.command)) { if (auto* u = cfg->child("username")) name = u->text + " " + name; }
            count[name]++; if (name.rfind("Couple ", 0) == 0) folders[r.where.folder]++;
            for (auto& [k, v] : c.properties) { props[name].insert(k); if (!example.count(name + k)) example[name + k] = v; }
        }
    }
    for (auto& [f, n] : folders) std::printf("couple orders in %s: %d\n", f.c_str(), n);
    std::multimap<int, std::string> by; for (auto& [k, v] : count) by.emplace(-v, k);
    for (auto& [n, k] : by) { std::printf("%5d %s:", -n, k.c_str()); for (auto& p : props[k]) std::printf(" %s='%s'", p.c_str(), example[k + p].c_str()); std::printf("\n"); }
}
