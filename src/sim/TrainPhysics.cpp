#include "TrainPhysics.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <map>

#include "content/EngineSpec.h"

namespace trainz::sim {

namespace {
constexpr double kG = 9.81;

using Curve = std::vector<std::pair<double, double>>;

// A force or mass from a spec: finite and not negative, else 0.
double nonNegative(double v) { return std::isfinite(v) && v > 0 ? v : 0; }

// Adds `add` into `into`: the sum is evaluated at every point of either curve, plus just past
// the end of a curve that ends with a force, so that force stops at its end instead of being
// spread over the gap to the next point (a curve gives 0 beyond its end; interpolateCurve).
void addCurve(Curve& into, const Curve& add) {
    if (add.empty()) {
        return;
    }
    std::map<double, double> sum;
    for (const Curve* c : std::initializer_list<const Curve*>{&into, &add}) {
        for (const auto& [s, f] : *c) {
            sum[s] = 0;
        }
        if (c->size() > 1 && c->back().second != 0) {
            sum[std::nextafter(c->back().first, std::numeric_limits<double>::infinity())] = 0;
        }
    }
    for (auto& [s, f] : sum) {
        f = trainz::content::interpolateCurve(into, s) + trainz::content::interpolateCurve(add, s);
    }
    into.assign(sum.begin(), sum.end());
}
}  // namespace

double stepTrain(double& speed, const TrainSpec& spec, const Controls& c, double grade, double seconds) {
    if (!std::isfinite(speed)) {
        speed = 0;
    }
    if (!(seconds > 0) || !std::isfinite(seconds) || !(spec.massKg > 0) || !std::isfinite(spec.massKg)) {
        return 0;
    }
    const double dtTotal = std::min(seconds, 1.0);
    const double throttle = std::isfinite(c.throttle) ? std::clamp(c.throttle, 0.0, 1.0) : 0.0;
    const double brake = std::isfinite(c.brake) ? std::clamp(c.brake, 0.0, 1.0) : 0.0;
    const double gr = std::isfinite(grade) ? std::clamp(grade, -0.5, 0.5) : 0.0;
    const int rev = c.reverser > 0 ? 1 : c.reverser < 0 ? -1 : 0;
    const double mu = std::isfinite(spec.adhesion) ? std::clamp(spec.adhesion, 0.0, 1.0) : 0.0;
    const double tractionGrip = mu * kG * std::min(nonNegative(spec.drivenMassKg), spec.massKg);
    const double brakeGrip = mu * kG * std::min(nonNegative(spec.brakedMassKg), spec.massKg);
    const double startForce = nonNegative(spec.maxTractiveForceN), power = nonNegative(spec.maxPowerW);
    const double brakeForce = nonNegative(spec.maxBrakeForceN);
    const double dA = nonNegative(spec.davisA), dB = nonNegative(spec.davisB), dC = nonNegative(spec.davisC);
    const bool useNotch = !spec.notchCurvesN.empty();
    // Dynamic brake: the locomotives' own curve for the dynamic notch (0 = off), limited by the
    // grip of the driven axles; while it is on there is no traction (one handle).
    const bool dynamicOn = c.dynamicNotch > 0 && static_cast<std::size_t>(c.dynamicNotch) < spec.dynamicBrakeCurvesN.size();
    auto dynamic = [&](double speedAbs) {
        return dynamicOn ? std::min(nonNegative(trainz::content::interpolateCurve(
                                        spec.dynamicBrakeCurvesN[static_cast<std::size_t>(c.dynamicNotch)], speedAbs)),
                                    tractionGrip)
                         : 0.0;
    };
    const bool notchOk = c.notch >= 0 && static_cast<std::size_t>(c.notch) < spec.notchCurvesN.size();
    // Locomotives without curves (starting force and power) pull alongside those with curves:
    // by the throttle, or by the notch as a fraction of the top notch.
    const double presetShare = useNotch ? (notchOk && spec.notchCurvesN.size() > 1
                                               ? static_cast<double>(c.notch) / static_cast<double>(spec.notchCurvesN.size() - 1)
                                               : 0.0)
                                        : throttle;
    double moved = 0;
    const int steps = std::max(1, static_cast<int>(std::ceil(dtTotal / 0.02)));
    const double dt = dtTotal / steps;
    for (int i = 0; i < steps; ++i) {
        const double v = speed, av = std::fabs(v);
        // Traction: notch curve, or smooth throttle × (curve / starting force / power), then grip.
        double traction = 0;
        if (useNotch) {
            if (notchOk) {
                traction = nonNegative(trainz::content::interpolateCurve(spec.notchCurvesN[static_cast<std::size_t>(c.notch)], av));
            }
        } else if (!spec.tractiveCurveN.empty()) {
            traction = throttle * nonNegative(trainz::content::interpolateCurve(spec.tractiveCurveN, av));
        }
        double preset = presetShare * startForce;
        if (av > 0.1) {
            preset = std::min(preset, presetShare * power / av);
        }
        traction += preset;
        if (dynamicOn) {
            traction = 0;
        }
        traction = std::min(traction, tractionGrip) * rev;
        const double gravity = -spec.massKg * kG * gr;
        const double braking = std::min(brake * brakeForce, brakeGrip) + dynamic(av);
        const double opposing = braking + dA + dB * av + dC * av * av;
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
        addCurve(t.tractiveCurveN, v.tractiveCurveN);
        t.brakedMassKg += v.braked ? v.massKg : 0;
        if (!v.source.empty() && t.source.find(v.source) == std::string::npos) {
            t.source += (t.source.empty() ? "" : "; ") + v.source;
        }
    }
    // Notches: the train has as many as its locomotive with the most. A locomotive with fewer
    // follows at the same fraction of its own range (train notch k of 0..N-1 -> its notch
    // round(k (n-1) / (N-1))), so all reach their top notch together. The port's choice; how
    // the game drives a mixed consist is not known. Dynamic-brake notches the same way.
    auto sumNotches = [&](std::vector<Curve>& into, auto member) {
        std::size_t top = 0;
        for (const auto& v : vehicles) {
            top = std::max(top, (v.*member).size());
        }
        into.assign(top, Curve{});
        for (const auto& v : vehicles) {
            const auto& own = v.*member;
            const std::size_t n = own.size();
            for (std::size_t k = 0; k < top && n > 0; ++k) {
                const std::size_t pick =
                    top == 1 ? 0 : static_cast<std::size_t>(std::lround(static_cast<double>(k * (n - 1)) / static_cast<double>(top - 1)));
                addCurve(into[k], own[std::min(pick, n - 1)]);
            }
        }
    };
    sumNotches(t.notchCurvesN, &VehicleSpec::notchCurvesN);
    sumNotches(t.dynamicBrakeCurvesN, &VehicleSpec::dynamicBrakeCurvesN);
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
