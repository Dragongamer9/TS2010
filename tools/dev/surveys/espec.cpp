#include "content/AssetIndex.h"
#include "content/AssetConfig.h"
#include "content/JaArchive.h"
#include "content/ContentError.h"
#include <cstdio>
#include <map>
#include <memory>
#include <functional>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    std::map<std::string, std::unique_ptr<JaArchive>> arch;
    std::map<std::string, int> keys; int specs = 0, shown = 0, traincarsWithMass = 0, traincars = 0, withSpec = 0;
    auto cfg = [&](const AssetRecord& r) -> std::optional<ConfigNode> {
        if (r.where.container != AssetLocation::Container::Archive) return std::nullopt;
        auto& a = arch[r.where.source.string()]; if (!a) a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
        const std::string base = r.where.folder.empty() ? "" : r.where.folder + "/";
        const JaEntry* ce = a->find(base + r.where.configName); if (!ce) return std::nullopt;
        try { return parseAssetConfig(a->readLimited(*ce, 16 << 20)).root; } catch (const ContentError&) { return std::nullopt; } };
    for (const auto& r : idx.records()) {
        if (r.kind == "traincar") { ++traincars; if (auto c = cfg(r)) { if (c->child("mass")) ++traincarsWithMass; if (c->child("enginespec")) ++withSpec; } }
        if (r.kind != "engine" && r.kind != "steam-engine") continue;
        auto c = cfg(r); if (!c) continue; ++specs;
        std::function<void(const ConfigNode&, std::string)> walk = [&](const ConfigNode& n, std::string path) {
            for (const auto& ch : n.children) { std::string p = path.empty() ? ch.key : path + "/" + ch.key;
                // collapse numbered children (curve points)
                std::string key = p; for (auto& cc : key) (void)cc;
                if (ch.type == ConfigType::Group) walk(ch, path.empty() ? ch.key : path + "/" + ch.key);
                else { std::string k = p; std::size_t sl = k.rfind('/'); std::string leaf = sl == std::string::npos ? k : k.substr(sl + 1);
                    bool numeric = !leaf.empty() && leaf.find_first_not_of("0123456789.-") == std::string::npos; if (numeric) k = k.substr(0, sl) + "/<n>";
                    keys[k + " (" + std::to_string(static_cast<int>(ch.type)) + ")"]++; } } };
        walk(*c, "");
        if (shown++ == 0) std::printf("sample enginespec: %s %s\n", formatKuid(r.kuid).c_str(), r.username.c_str());
    }
    std::printf("%d traincars (%d with mass, %d with enginespec); %d enginespec assets\n", traincars, traincarsWithMass, withSpec, specs);
    for (auto& [k, v] : keys) if (v * 10 >= specs) std::printf("%5d  %s\n", v, k.c_str());
}
