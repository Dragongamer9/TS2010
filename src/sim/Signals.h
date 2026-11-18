// A simple signal system on a TrackNetwork: the port's own rules, not the game's (TS2010's
// signal logic is not decoded; docs/formats/junctions-signals.md).
//
// Each signal stands at a track position and is for travel in that position's direction. Its
// block runs from it, along the current junction settings, to the next signal for the same
// direction (or a line end, or kMaxBlock metres). Aspects:
//   Stop    - something occupies the block;
//   Caution - the block is clear but the next signal shows Stop;
//   Clear   - otherwise.
#pragma once

#include "sim/TrackNetwork.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace trainz::sim {

enum class Aspect { Stop, Caution, Clear };
const char* aspectName(Aspect a);

struct Signal {
    TrackPosition at;   // direction = the travel it is for
    std::string name;   // for display ("" if none)
    long object = -1;   // index of the placed object it came from, if any
};

// A point taken by a train (e.g. each vehicle's ends).
struct Occupied {
    std::int32_t piece = -1;
    double offset = 0;
};

class SignalSystem {
public:
    SignalSystem(const TrackNetwork& net, std::vector<Signal> signals);

    // Recomputes every aspect from the occupied points and the network's current junctions.
    void update(const std::vector<Occupied>& occupied);
    Aspect aspect(std::size_t i) const { return aspects_.at(i); }
    const std::vector<Signal>& signals() const { return signals_; }

    // The next signal for a train at `p` (for its direction of travel), within maxDistance.
    struct Ahead {
        std::size_t index = 0;
        double distance = 0;
    };
    std::optional<Ahead> nextSignal(const TrackPosition& p, double maxDistance = 2000) const;

    static constexpr double kMaxBlock = 5000;  // metres

private:
    // The first signal for this direction strictly past `start` (more than 1 cm ahead).
    std::optional<Ahead> firstAhead(const TrackPosition& start, double maxDistance) const;

    const TrackNetwork& net_;
    std::vector<Signal> signals_;
    std::vector<Aspect> aspects_;
    std::multimap<std::int32_t, std::size_t> byPiece_;
};

}  // namespace trainz::sim
