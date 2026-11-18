#include "content/AssetIndex.h"
#include <cstdio>
#include <map>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    for (const auto* r : idx.find(*parseKuidLoose(argv[2]))) std::printf("%s kind '%s' name '%s' in %s/%s\n", formatKuid(r->kuid).c_str(), r->kind.c_str(), r->username.c_str(), r->where.source.filename().c_str(), r->where.folder.c_str());
    std::map<std::string,int> kinds; for (const auto& r : idx.records()) kinds[r.kind]++;
    for (auto& [k, v] : kinds) std::printf("  %s:%d", k.c_str(), v); std::printf("\n");
}
