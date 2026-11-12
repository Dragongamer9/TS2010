// A traincar's physics data from the original files: the traincar config's `mass` (kg) and
// the `enginespec` asset it names (kind "engine", or "steam-engine"). Keys and units as the
// Trainz wiki documents them (docs/research/public-formats.md): `throttle-power` holds one curve
// per throttle notch of speed (m/s, the child's key) -> tractive force (kN, its value); the
// `motor` container's DCC-mode values are kept but their units are not established.
// OBSERVED on the disc: all 1,203 traincars have `mass` and `enginespec`; 187 such assets.
// UNRESOLVED: the curves' forces look large for kN (an SD40-2's notch 6 starts at 990, about
// 2.5x its real starting tractive effort).
#pragma once

#include "content/AssetConfig.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace trainz::content {

struct EngineSpec {
    // motor container (as written; meaning of the units not established)
    std::optional<double> maxAccel, maxDecel, adhesion, resistance, maxSpeed, brakeRatio;
    std::optional<int> axleCount, throttleNotches;
    // throttle-power: per notch (index = notch), points (speed m/s, force kN), sorted by speed.
    std::vector<std::vector<std::pair<double, double>>> throttlePower;
    std::vector<std::vector<std::pair<double, double>>> dynamicBrake;

    // The force (N) of a notch at a speed, interpolated linearly (0 past the curve's end or for
    // a missing notch).
    double tractiveForceN(std::size_t notch, double speed) const;
    // The highest notch's curve in N: the "full throttle" curve (empty without throttle-power).
    std::vector<std::pair<double, double>> fullThrottleCurveN() const;
};

// Reads an enginespec config (missing or odd values are left empty).
EngineSpec parseEngineSpec(const ConfigNode& root);

// Linear interpolation in a (x, y) curve sorted by x: the first y below the first point, 0
// beyond the last (a curve ends where its force ends), and 0 for an empty curve or a NaN x.
// A single point is a constant.
double interpolateCurve(const std::vector<std::pair<double, double>>& curve, double x);

}  // namespace trainz::content
