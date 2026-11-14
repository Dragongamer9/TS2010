#include "RouteLoader.h"

#include "content/ContentError.h"
#include "content/ObjectContainer.h"

namespace trainz::view {

using namespace trainz::content;

RouteExtras loadRouteExtras(const JaArchive& archive, const std::string& routeFolder, const std::string& sessionArchive,
                            const std::string& sessionFolder) {
    RouteExtras r;
    if (const JaEntry* te = archive.find(routeFolder + "/mapfile.trk")) {
        r.track = readTrackLayout(parseRouteObjectFile(archive.readLimited(*te, std::size_t{256} << 20)));
    }
    if (const JaEntry* oe = archive.find(routeFolder + "/mapfile.obs")) {
        for (const auto& c : parseRouteObjectFile(archive.readLimited(*oe, std::size_t{256} << 20)).containers) {
            for (const auto& item : c.items) {
                if (auto o = decodePlacedObject(item)) {
                    r.objects.push_back(std::move(*o));
                }
            }
        }
    }
    if (!sessionArchive.empty()) {
        const auto sa = JaArchive::open(sessionArchive);
        const JaEntry* pe = sa.find(sessionFolder + "/profile.dat");
        if (pe == nullptr) {
            throw ContentError("'" + sessionFolder + "/profile.dat' is not in " + sessionArchive);
        }
        r.trains.emplace();
        // The session's own .obs: objects it adds to the route (observed: trackmarks, signals...).
        for (const auto& e : sa.entries()) {
            const std::string prefix = sessionFolder + "/";
            if (e.name.rfind(prefix, 0) != 0 || e.name.size() < prefix.size() + 5 || e.name.substr(e.name.size() - 4) != ".obs" ||
                e.name.find('/', prefix.size()) != std::string::npos) {
                continue;
            }
            for (const auto& c : parseRouteObjectFile(sa.readLimited(e, std::size_t{256} << 20)).containers) {
                for (const auto& item : c.items) {
                    if (auto p = decodePlacedObject(item)) {
                        r.sessionObjects.push_back(std::move(*p));
                    }
                }
            }
            break;
        }
        for (const auto& blk : parseSessionProfile(sa.readLimited(*pe, std::size_t{256} << 20)).blocks) {
            if (blk.container) {
                for (const auto& item : blk.container->items) {
                    if (auto t = decodeTrain(item)) {
                        r.trains->push_back(std::move(*t));
                    } else if (auto b = decodeBehaviour(item)) {
                        r.rules.push_back(std::move(*b));
                    }
                }
            }
        }
    }
    return r;
}

}  // namespace trainz::view
