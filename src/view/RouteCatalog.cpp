#include "RouteCatalog.h"

#include "content/AssetConfig.h"
#include "content/ContentError.h"
#include "content/JaArchive.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>

namespace trainz::view {

using namespace trainz::content;

namespace {

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

CatalogEntry entryOf(const AssetRecord& r) {
    return CatalogEntry{r.kuid, r.username.empty() ? formatKuid(r.kuid) : r.username, r.where};
}

}  // namespace

std::vector<CatalogRoute> listRoutes(const AssetIndex& index) {
    std::vector<CatalogRoute> routes;
    std::map<std::string, std::size_t> byKuid;  // formatted route KUID -> routes[]
    for (const auto& r : index.records()) {
        if (r.kind == "map" && r.where.container == AssetLocation::Container::Archive) {
            byKuid.emplace(formatKuid(r.kuid), routes.size());
            routes.push_back(CatalogRoute{entryOf(r), {}});
        }
    }
    std::map<std::string, std::unique_ptr<JaArchive>> archives;
    for (const auto& r : index.records()) {
        if (r.kind != "profile" || r.where.container != AssetLocation::Container::Archive) {
            continue;
        }
        try {
            auto& a = archives[r.where.source.string()];
            if (!a) {
                a = std::make_unique<JaArchive>(JaArchive::open(r.where.source));
            }
            const std::string base = r.where.folder.empty() ? std::string() : r.where.folder + "/";
            const JaEntry* ce = a->find(base + r.where.configName);
            if (ce == nullptr) {
                continue;
            }
            const ConfigNode root = parseAssetConfig(a->readLimited(*ce, std::size_t{16} << 20)).root;
            const ConfigNode* m = root.child("map-kuid");
            const auto map = m != nullptr ? m->asKuid() : std::nullopt;
            if (!map) {
                continue;
            }
            // The route's own KUID, or another version of it.
            auto it = byKuid.find(formatKuid(map->normalized()));
            if (it == byKuid.end()) {
                for (const AssetRecord* v : index.allVersions(map->user, map->content)) {
                    if (v->kind == "map") {
                        it = byKuid.find(formatKuid(v->kuid));
                        break;
                    }
                }
            }
            if (it != byKuid.end()) {
                routes[it->second].sessions.push_back(entryOf(r));
            }
        } catch (const ContentError&) {
            // an unreadable archive or config: leave that session out
        }
    }
    auto byName = [](const CatalogEntry& a, const CatalogEntry& b) { return lower(a.name) < lower(b.name); };
    for (auto& r : routes) {
        std::sort(r.sessions.begin(), r.sessions.end(), byName);
    }
    std::sort(routes.begin(), routes.end(), [&](const CatalogRoute& a, const CatalogRoute& b) { return byName(a.route, b.route); });
    return routes;
}

const CatalogEntry* findEntry(const std::vector<const CatalogEntry*>& entries, const std::string& query,
                              std::vector<std::string>& candidates) {
    candidates.clear();
    // One match: it. Several: none, with every candidate (name and KUID) listed.
    auto pick = [&](const std::vector<const CatalogEntry*>& found) -> const CatalogEntry* {
        if (found.size() == 1) {
            return found.front();
        }
        for (const CatalogEntry* e : found) {
            candidates.push_back(e->name + "  " + formatKuid(e->kuid));
        }
        return nullptr;
    };
    if (const auto k = parseKuidLoose(query)) {
        std::vector<const CatalogEntry*> exact, anyVersion;
        for (const CatalogEntry* e : entries) {
            if (e->kuid.normalized() == k->normalized()) {
                exact.push_back(e);
            } else if (e->kuid.user == k->user && e->kuid.content == k->content) {
                anyVersion.push_back(e);
            }
        }
        return exact.empty() ? pick(anyVersion) : pick(exact);
    }
    const std::string q = lower(query);
    if (q.empty()) {
        return nullptr;
    }
    std::vector<const CatalogEntry*> exact, partial;
    for (const CatalogEntry* e : entries) {
        const std::string n = lower(e->name);
        if (n == q) {
            exact.push_back(e);
        } else if (n.find(q) != std::string::npos) {
            partial.push_back(e);
        }
    }
    return exact.empty() ? pick(partial) : pick(exact);
}

}  // namespace trainz::view
