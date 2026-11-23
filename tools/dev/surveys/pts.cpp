#include "content/AssetIndex.h"
#include "view/AssetMeshes.h"
#include <cstdio>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    trainz::view::AssetMeshes m(idx);
    for (int i = 2; i < argc; ++i) {
        const auto* p = m.get(*parseKuidLoose(argv[i]));
        std::printf("%s: %s\n", argv[i], p ? "" : "no model");
        if (p) for (auto& [n, f] : p->attachments) std::printf("  %s at %.2f %.2f %.2f\n", n.c_str(), f.t[0], f.t[1], f.t[2]);
    }
}
