// Which placed object's model covers screen pixel (px, py) for a camera (960x540, fov 50)?
#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "view/AssetMeshes.h"
#include "view/RouteCatalog.h"
#include "view/RouteLoader.h"
#include "view/SceneRender.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
using namespace trainz::content;
using namespace trainz::view;
int main(int argc, char** argv) {
    AssetIndex idx = AssetIndex::scanInstall(argv[1]);
    AssetMeshes meshes(idx);
    double c[6]; for (int i = 0; i < 6; ++i) c[i] = std::atof(argv[3 + i]);
    const double PX = std::atof(argv[9]), PY = std::atof(argv[10]);
    const double W = 960, H = 540;
    double f[3] = {c[3] - c[0], c[4] - c[1], c[5] - c[2]}; double fl = std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]); for (auto& v : f) v /= fl;
    double r[3] = {f[1], -f[0], 0}; double rl = std::hypot(r[0], r[1]); r[0] /= rl; r[1] /= rl;
    double u[3] = {r[1]*f[2]-r[2]*f[1], r[2]*f[0]-r[0]*f[2], r[0]*f[1]-r[1]*f[0]};
    const double F = (H / 2) / std::tan(std::atof(getenv("FOV") ? getenv("FOV") : "50") * M_PI / 360);
    auto proj = [&](const ScenePoint& p, double& sx, double& sy, double& d) { double q[3] = {p.x-c[0], p.y-c[1], p.z-c[2]}; d = q[0]*f[0]+q[1]*f[1]+q[2]*f[2]; if (d <= 1) return false;
        sx = W/2 + F*(q[0]*r[0]+q[1]*r[1]+q[2]*r[2])/d; sy = H/2 - F*(q[0]*u[0]+q[1]*u[1]+q[2]*u[2])/d; return true; };
    for (const auto& rt : listRoutes(idx)) {
        if (rt.route.name.find(argv[2]) == std::string::npos) continue;
        JaArchive a = JaArchive::open(rt.route.where.source);
        auto ex = loadRouteExtras(a, rt.route.where.folder, "", "");
        for (const auto& o : ex.objects) {
            if (std::hypot(o.worldX() - c[0], o.worldY() - c[1]) > 700) continue;
            const auto* m = meshes.get(o.kuid); if (!m) continue;
            Scene s; addPlacedMesh(s, *m, o.worldX(), o.worldY(), o.z, o.yaw);
            double best = 1e18;
            for (const auto& t : s.triangles) { double x[3], y[3], d[3]; bool ok = true; for (int i = 0; i < 3; ++i) ok = ok && proj(t.p[i], x[i], y[i], d[i]); if (!ok) continue;
                double ar = (x[1]-x[0])*(y[2]-y[0])-(x[2]-x[0])*(y[1]-y[0]); if (std::fabs(ar) < 1e-9) continue;
                double w0 = ((x[1]-PX)*(y[2]-PY)-(x[2]-PX)*(y[1]-PY))/ar, w1 = ((x[2]-PX)*(y[0]-PY)-(x[0]-PX)*(y[2]-PY))/ar, w2 = 1-w0-w1;
                if (w0 >= 0 && w1 >= 0 && w2 >= 0) best = std::min(best, w0*d[0]+w1*d[1]+w2*d[2]); }
            if (best < 1e17) { auto rs = idx.find(o.kuid); float lo = 1e9, hi = -1e9; for (auto& t : m->triangles) for (auto& p : t.p) { lo = std::min(lo, p[2]); hi = std::max(hi, p[2]); }
                std::printf("depth %.1f  %s %s kind %s at %.1f %.1f z %.1f, model z %.1f..%.1f, %zu tris%s%s mesh %s\n", best, formatKuid(o.kuid).c_str(), o.tag.c_str(), rs.empty() ? "?" : rs.front()->kind.c_str(), o.worldX(), o.worldY(), o.z, lo, hi, m->triangles.size(), m->placeholder ? " PLACEHOLDER" : "", m->billboard ? " BILLBOARD" : "", m->meshName.c_str()); }
        }
    }
}
