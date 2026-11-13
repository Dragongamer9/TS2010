// A train's motion along its track: the forces from the driver's controls, resistance and
// gradient, integrated over time. A plain model of our own (not taken from any game):
// traction limited by the starting force, by power at the wheel and by adhesion on the driven
// axles; brakes limited by their force and by adhesion on the braked axles; Davis resistance
// R = A + B v + C v^2 (N, v in m/s, summed over the vehicles); gravity on a grade.
// Sources for the preset numbers: docs/research/train-physics.md.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace trainz::sim {

struct TrainSpec {
    double massKg = 400'000;
    double maxTractiveForceN = 300'000;  // starting tractive effort
    double maxPowerW = 6'400'000;        // at the wheel: force is capped at power / speed
    double maxBrakeForceN = 0.12 * 9.81 * 400'000;  // full (emergency) brake
    double drivenMassKg = 90'000;  // on the driven axles (adhesion for traction)
    double brakedMassKg = 400'000;  // on the braked axles (adhesion for braking)
    double adhesion = 0.25;         // wheel-rail friction coefficient (dry ~0.25, wet ~0.18)
    double davisA = 6'000;  // N
    double davisB = 120;    // N per m/s
    double davisC = 9;      // N per (m/s)^2
    // Optional: full-throttle tractive force (N) against speed (m/s), e.g. from the train's own
    // enginespec curves (summed over its locomotives). Used (scaled by the throttle) when
    // notchCurvesN is empty. Locomotives without curves add their starting-force/power pull
    // (maxTractiveForceN, maxPowerW) on top; the adhesion limit applies to the total.
    std::vector<std::pair<double, double>> tractiveCurveN;
    // Optional: one curve per throttle notch (N vs m/s), summed over locomotives (one with fewer
    // notches follows at the same fraction of its range; see makeTrain). When non-empty,
    // Controls::notch selects the curve, the smooth throttle is ignored, and locomotives without
    // curves pull at notch ÷ top notch of their starting force/power.
    std::vector<std::vector<std::pair<double, double>>> notchCurvesN;
    // Optional: dynamic-brake force (N) per dynamic notch against speed (m/s), summed like the
    // notch curves (from each locomotive's enginespec `dynamic-brake`). Notch 0 is off.
    std::vector<std::vector<std::pair<double, double>>> dynamicBrakeCurvesN;
    std::string source;     // where the numbers come from (or "uncalibrated")
};

struct Controls {
    double throttle = 0;  // 0..1 (smooth path when the train has no notch curves)
    double brake = 0;     // 0..1 of the full brake force
    int reverser = +1;    // +1 forward, -1 reverse, 0 neutral (no traction)
    int notch = 0;        // index into TrainSpec::notchCurvesN (0 = first / idle notch)
    int dynamicNotch = 0;  // index into TrainSpec::dynamicBrakeCurvesN; 0 = off (then no traction while on)
};

// Advances the speed (m/s, signed: + along the train's facing) over `seconds` on a track
// rising `grade` metres per metre travelled in the direction the train faces (the sine of the
// slope, as TrackNetwork's TrackPoint::grade). Brakes and resistance never push a train
// backwards through zero, and hold it only up to their (adhesion-limited) force; gravity
// beyond that rolls it back. Returns the distance moved (signed).
double stepTrain(double& speed, const TrainSpec& spec, const Controls& controls, double grade, double seconds);

// --- Building a train from its vehicles -----------------------------------------------
struct VehicleSpec {
    std::string name;
    double massKg = 0;
    double davisA = 0, davisB = 0, davisC = 0;  // this vehicle's share, N, N s/m, N s^2/m^2
    double tractiveForceN = 0, powerW = 0;      // 0 for unpowered vehicles
    double brakeForceN = 0;                     // full brake
    bool driven = false, braked = true;
    std::vector<std::pair<double, double>> tractiveCurveN;  // optional (see TrainSpec)
    std::vector<std::vector<std::pair<double, double>>> notchCurvesN;  // optional per-notch curves
    std::vector<std::vector<std::pair<double, double>>> dynamicBrakeCurvesN;  // optional (see TrainSpec)
    std::string source;
};
// Sums masses, resistances and forces; the adhesion coefficient is the given one. Negative or
// non-finite forces, masses and resistances in a spec count as 0 in stepTrain, and a train with
// a non-finite mass does not move.
TrainSpec makeTrain(const std::vector<VehicleSpec>& vehicles, double adhesion = 0.25);

// Presets. Each says where its numbers come from in `source`.
// A modern mainline electric locomotive (Siemens Vectron AC ratings: 300 kN starting
// tractive effort, 6,400 kW at the wheel). Its mass (90 t), resistance and brake force are
// UNCALIBRATED choices.
VehicleSpec electricLocomotive();
// A freight wagon by the AAR RP-548 resistance formula in Szanto (2016), converted to m/s:
// A = 6.5 m + 80 n, B = 0.1656 m, C = 1.24416 (m in tonnes, n axles). Its brake force (a
// 0.52 m/s^2 equivalent, from a UIC 544-1 emergency stop of a 1,689 t P-position train) is
// an EMERGENCY figure, not ordinary service braking.
VehicleSpec freightWagon(double massTonnes, int axles = 4);
// A complete Shinkansen Series 100 train on ballasted track (886 t; Rochard & Schmid 2000,
// eq. 24, from a Scribd copy, not checked against the publisher's): A 11,060 N, B 223.2,
// C 15.6168. Brake: 12% g emergency demand (Alturbeh et al.). Traction UNCALIBRATED.
TrainSpec shinkansenSeries100();
// Passenger brake demands as fractions of g, notch 1-3 and emergency (Alturbeh et al.):
// demands, not guaranteed decelerations (adhesion and resistance change the result).
inline constexpr double kPassengerBrakeNotchG[4] = {0.03, 0.06, 0.09, 0.12};

}  // namespace trainz::sim
