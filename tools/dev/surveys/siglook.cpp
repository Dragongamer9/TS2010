#include "content/AssetIndex.h"
#include "content/SignalAsset.h"
#include "view/AssetMeshes.h"
#include <cstdio>
#include <map>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    trainz::view::AssetMeshes m(idx);
    std::map<std::string, int> h;
    for (const auto& r : idx.records()) {
        if (r.kind != "mosignal") continue;
        h["mosignal assets"]++;
        auto cfg = m.config(r.kuid); if (!cfg) { h["config unreadable"]++; continue; }
        auto look = parseSignalLook(*cfg);
        if (look.states.empty()) { h["no signals container"]++; continue; }
        h["with states"]++;
        const auto* mesh = m.get(r.kuid); if (!mesh) { h["  no model"]++; continue; }
        bool all = true, any = false;
        for (int a = 0; a < 3; ++a) { auto st = stateForAspect(look, a); for (int l : look.states.at(*st)) { if (l < 0) continue; bool f = mesh->attachments.count("a.light" + std::to_string(l)); all = all && f; any = any || f; } }
        h[all ? "  all lit lights on the model" : any ? "  some lights missing" : "  no light points"]++;
        for (auto& [s, l] : look.states) h["state " + std::to_string(s)]++;
    }
    for (auto& [k, n] : h) std::printf("%6d  %s\n", n, k.c_str());
}
