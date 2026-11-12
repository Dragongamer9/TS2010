#include "TrainPhysics.h"

#include <algorithm>
#include <cmath>

namespace trainz::sim {

double stepTrain(double& speed, const TrainSpec& spec, const Controls& c, double grade, double seconds) {
    if (!std::isfinite(speed)) {
        speed = 0;
    }
    if (!(seconds > 0) || !std::isfinite(seconds) || !(spec.massKg > 0)) {
        return 0;
    }
    const double dtTotal = std::min(seconds, 1.0);
    const double g = 9.80665;
    const double throttle = std::isfinite(c.throttle) ? std::clamp(c.throttle, 0.0, 1.0) : 0.0;
    const double brake = std::isfinite(c.brake) ? std::clamp(c.brake, 0.0, 1.0) : 0.0;
    const double gr = std::isfinite(grade) ? std::clamp(grade, -0.2, 0.2) : 0.0;
    const int rev = c.reverser > 0 ? 1 : c.reverser < 0 ? -1 : 0;
    double moved = 0;
    // Small steps keep the stop-at-zero handling and the power limit well behaved.
    const int steps = std::max(1, static_cast<int>(std::ceil(dtTotal / 0.02)));
    const double dt = dtTotal / steps;
    for (int i = 0; i < steps; ++i) {
        const double v = speed, av = std::fabs(v);
        // Traction, in the reverser's direction; above the power limit force = P / v.
        double traction = throttle * spec.maxTractiveForceN;
        if (av > 0.1) {
            traction = std::min(traction, throttle * spec.maxPowerW / av);
        }
        traction *= rev;
        const double gravity = -spec.massKg * g * gr;  // gr is rise per metre travelled (the sine)
        // Brakes and rolling resistance oppose motion; at a standstill they hold the train
        // against the other forces up to their size (no creeping backwards on the brakes).
        const double opposing = brake * spec.maxBrakeForceN + spec.davisA + spec.davisB * av + spec.davisC * av * av;
        const double driving = traction + gravity;
        double force;
        if (av < 1e-6) {
            force = std::fabs(driving) <= opposing ? 0 : driving - std::copysign(opposing, driving);
        } else {
            force = driving - std::copysign(opposing, v);
        }
        double nv = v + force / spec.massKg * dt;
        if (av > 1e-6 && (nv > 0) != (v > 0) && std::fabs(driving) <= opposing) {
            nv = 0;  // resistance and brakes stop it; they don't reverse it
        }
        moved += 0.5 * (v + nv) * dt;
        speed = nv;
    }
    return moved;
}

}  // namespace trainz::sim
