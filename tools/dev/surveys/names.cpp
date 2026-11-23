#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "view/RouteCatalog.h"
#include "view/RouteLoader.h"
#include "sim/TrackNetwork.h"
#include "game/RouteDrive.h"
#include <cstdio>
using namespace trainz::content;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    for (const auto& rt : trainz::view::listRoutes(idx)) for (const auto& se : rt.sessions) {
        if (se.name != argv[2]) continue;
        JaArchive a = JaArchive::open(rt.route.where.source);
        auto ex = trainz::view::loadRouteExtras(a, rt.route.where.folder, se.where.source.string(), se.where.folder);
        for (std::size_t t = 0; t < ex.trains->size(); ++t) { std::printf("train %zu:", t + 1); for (auto& v : (*ex.trains)[t].vehicles) std::printf(" [%s]", v.name.c_str()); std::printf("\n"); }
        for (auto& o : trainz::game::driverOrders(ex.rules)) { std::printf("driver for [%s]:", o.vehicle.c_str()); for (auto& c : o.commands) for (auto& [k, v] : c.properties) std::printf(" %s=%s", k.c_str(), v.c_str()); std::printf("\n"); }
        trainz::sim::TrackNetwork net(*ex.track); int n = 0;
        for (auto& [k, v] : trainz::game::routeTrackmarks(net, ex.objects)) if (n++ < 30) std::printf("mark [%s] ", k.c_str());
        std::printf("\n");
    }
}
