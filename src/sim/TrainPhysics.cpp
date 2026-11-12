#include "TrainPhysics.h"

#include <algorithm>
#include <cmath>

namespace trainz::sim {

namespace {
constexpr double kG = 9.81;
}

double stepTrain(double& speed, const TrainSpec& spec, const Controls& c, double grade, double seconds) {
    if (!std::isfinite(speed)) {
        speed = 0;
    }
    if (!(seconds > 0) || !std::isfinite(seconds) || !(spec.massKg > 0)) {
        return 0;
    }
    const double dtTotal = std::min(seconds, 1.0);
    const double throttle = std::isfinite(c.throttle) ? std::clamp(c.throttle, 0.0, 1.0) : 0.0;
    const double brake = std::isfinite(c.brake) ? std::clamp(c.brake, 0.0, 1.0) : 0.0;
    const double gr = std::isfinite(grade) ? std::clamp(grade, -0.5, 0.5) : 0.0;
    const int rev = c.reverser > 0 ? 1 : c.reverser < 0 ? -1 : 0;
    const double mu = std::isfinite(spec.adhesion) ? std::clamp(spec.adhesion, 0.0, 1.0) : 0.0;
    const double tractionGrip = mu * kG * std::max(0.0, spec.drivenMassKg);
    const double brakeGrip = mu * kG * std::max(0.0, spec.brakedMassKg);
    double moved = 0;
    const int steps = std::max(1, static_cast<int>(std::ceil(dtTotal / 0.02)));
    const double dt = dtTotal / steps;
    for (int i = 0; i < steps; ++i) {
        const double v = speed, av = std::fabs(v);
        // Traction: starting force, then power / speed, never more than the grip of the driven axles.
        double traction = throttle * spec.maxTractiveForceN;
        if (av > 0.1) {
            traction = std::min(traction, throttle * spec.maxPowerW / av);
        }
        traction = std::min(traction, tractionGrip) * rev;
        const double gravity = -spec.massKg * kG * gr;
        const double braking = std::min(brake * spec.maxBrakeForceN, brakeGrip);
        const double opposing = braking + spec.davisA + spec.davisB * av + spec.davisC * av * av;
        const double driving = traction + gravity;
        double force;
        if (av < 1e-6) {
            // At a standstill brakes and resistance hold the train up to their size.
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

TrainSpec makeTrain(const std::vector<VehicleSpec>& vehicles, double adhesion) {
    TrainSpec t{};
    t.massKg = t.maxTractiveForceN = t.maxPowerW = t.maxBrakeForceN = 0;
    t.drivenMassKg = t.brakedMassKg = 0;
    t.davisA = t.davisB = t.davisC = 0;
    t.adhesion = adhesion;
    for (const auto& v : vehicles) {
        t.massKg += v.massKg;
        t.davisA += v.davisA, t.davisB += v.davisB, t.davisC += v.davisC;  // total forces add up
        t.maxTractiveForceN += v.tractiveForceN;
        t.maxPowerW += v.powerW;
        t.maxBrakeForceN += v.brakeForceN;
        t.drivenMassKg += v.driven ? v.massKg : 0;
        t.brakedMassKg += v.braked ? v.massKg : 0;
        if (!v.source.empty() && t.source.find(v.source) == std::string::npos) {
            t.source += (t.source.empty() ? "" : "; ") + v.source;
        }
    }
    return t;
}

VehicleSpec electricLocomotive() {
    VehicleSpec v;
    v.name = "electric locomotive";
    v.massKg = 90'000;  // UNCALIBRATED
    v.tractiveForceN = 300'000;
    v.powerW = 6'400'000;
    v.davisA = 6.5 * 90 + 80 * 4, v.davisB = 0.1656 * 90, v.davisC = 4.0;  // UNCALIBRATED (wagon formula, bigger front)
    v.brakeForceN = 0.12 * kG * v.massKg;                                      // UNCALIBRATED
    v.driven = true;
    v.source = "Siemens Vectron AC ratings (traction, power); mass, resistance, brake uncalibrated";
    return v;
}

VehicleSpec freightWagon(double massTonnes, int axles) {
    VehicleSpec v;
    v.name = "freight wagon";
    const double m = std::isfinite(massTonnes) ? std::clamp(massTonnes, 1.0, 200.0) : 20.0;
    const int n = std::clamp(axles, 2, 12);
    v.massKg = m * 1000;
    v.davisA = 6.5 * m + 80 * n;
    v.davisB = 0.1656 * m;
    v.davisC = 1.24416;
    v.brakeForceN = 0.52 * v.massKg;  // emergency-stop equivalent, not service braking
    v.source = "AAR RP-548 resistance (Szanto 2016); emergency brake equivalent (Bosso et al. 2023, UIC 544-1)";
    return v;
}

TrainSpec shinkansenSeries100() {
    TrainSpec t;
    t.massKg = 886'000;  // the paper's text; its Table 1 says 856 t
    t.davisA = 11'060, t.davisB = 223.2, t.davisC = 15.6168;
    t.maxTractiveForceN = 300'000;  // UNCALIBRATED
    t.maxPowerW = 11'040'000;       // UNCALIBRATED
    t.maxBrakeForceN = 0.12 * kG * t.massKg;
    t.drivenMassKg = t.brakedMassKg = t.massKg;  // multiple unit: assumed all axles driven and braked
    t.adhesion = 0.25;
    t.source = "Rochard & Schmid (2000) eq. 24, Series 100 ballasted track (Scribd copy); brake 12% g (Alturbeh et al.); traction uncalibrated";
    return t;
}

}  // namespace trainz::sim
