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
        for (const auto& blk : parseSessionProfile(sa.readLimited(*pe, std::size_t{256} << 20)).blocks) {
            if (blk.container) {
                for (const auto& item : blk.container->items) {
                    if (auto t = decodeTrain(item)) {
                        r.trains->push_back(std::move(*t));
                    }
                }
            }
        }
    }
    return r;
}

}  // namespace trainz::view
