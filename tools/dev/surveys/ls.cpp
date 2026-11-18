#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include <cstdio>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    for (int i = 2; i < argc; ++i) {
        auto rs = idx.find(*parseKuidLoose(argv[i])); if (rs.empty()) continue;
        const auto* r = rs.front(); JaArchive a = JaArchive::open(r->where.source);
        for (const auto& e : a.entries()) if (e.name.rfind(r->where.folder + "/", 0) == 0) std::printf("%s %zu\n", e.name.c_str(), static_cast<std::size_t>(e.originalSize));
    }
}
