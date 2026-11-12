// A train's motion along its track: the forces from the driver's controls, resistance and
// gradient, integrated over time. A plain, documented model of our own (not taken from any
// game): tractive effort limited by adhesion-like maximum force and by power, brakes, the
// Davis resistance formula R = A + B v + C v^2, and gravity on a grade.
#pragma once

namespace trainz::sim {

struct TrainSpec {
    double massKg = 400'000;
    double maxTractiveForceN = 300'000;  // at low speed
    double maxPowerW = 3'000'000;        // tractive force is capped at power / speed above that
    double maxBrakeForceN = 400'000;
    double davisA = 6'000;  // N
    double davisB = 120;    // N per m/s
    double davisC = 9;      // N per (m/s)^2
};

struct Controls {
    double throttle = 0;  // 0..1
    double brake = 0;     // 0..1
    int reverser = +1;    // +1 forward, -1 reverse, 0 neutral (no traction)
};

// Advances the speed (m/s, signed: + along the train's facing) over `seconds` on a track
// rising `grade` metres per metre travelled in the direction the train faces (the sine of the
// slope, as TrackNetwork's TrackPoint::grade). Brakes and resistance
// never push a train backwards through zero; gravity can. Returns the distance moved (signed).
double stepTrain(double& speed, const TrainSpec& spec, const Controls& controls, double grade, double seconds);

}  // namespace trainz::sim
