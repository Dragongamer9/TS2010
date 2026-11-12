// Driving along track: a position on a track piece as metres along its curve, moved forward or
// back across vertices into the next piece. Built from a TrackLayout (the port's curve model,
// content/TrackLayout.h), so it works for decoded routes and for the game's own track alike.
// At a vertex a train continues onto a piece on the OTHER side of the vertex (link flags are
// vertex sides); at a junction it takes the piece chosen by the junction setting (default: the
// one that turns least). A line end stops it (a buffer).
#pragma once

#include "content/TrackLayout.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace trainz::sim {

struct TrackPosition {
    std::int32_t piece = -1;
    double offset = 0;   // metres along the piece's curve from its `from` vertex
    int direction = +1;  // +1: moving forward goes toward `to`; -1: toward `from`
};

struct TrackPoint {
    double x = 0, y = 0, z = 0;  // world metres
    double heading = 0;          // radians, the direction of travel (direction applied)
    double grade = 0;            // rise per metre of travel (dz / ds)
};

class TrackNetwork {
public:
    explicit TrackNetwork(const trainz::content::TrackLayout& layout);

    // The piece's length along its curve (metres), or nullopt if it is not in the network.
    std::optional<double> length(std::int32_t piece) const;
    std::optional<TrackPoint> point(const TrackPosition& p) const;

    struct Move {
        double moved = 0;          // metres actually moved (less than asked at a line end)
        bool stoppedAtEnd = false;  // ran into a line end
    };
    // Moves `metres` along the direction of travel (negative: backwards). Junctions: a piece
    // index in `switches` (by vertex) picks the way, else the one that turns least.
    Move advance(TrackPosition& p, double metres) const;

    // The track position nearest to a world point (x, y), facing toward the piece's `to` end,
    // or nullopt if no piece comes within maxDistance metres. Checks every piece's samples.
    std::optional<TrackPosition> locate(double x, double y, double maxDistance = 5.0) const;

    // Which way a train leaving `vertex` on this side may go: the pieces on the other side.
    std::vector<std::int32_t> exits(std::int32_t vertex, std::int32_t arrivingPiece) const;

    std::map<std::int32_t, std::int32_t> switches;  // vertex -> chosen piece at that junction

    static constexpr int kSamples = 32;  // curve samples per piece for arc length

private:
    struct Piece {
        std::int32_t from = 0, to = 0;
        trainz::content::TrackCurve curve;
        std::vector<double> cumulative;  // arc length at sample k (kSamples + 1 values)
    };
    double paramAt(const Piece& pc, double offset) const;  // curve parameter 0..1 for an arc length
    std::map<std::int32_t, Piece> pieces_;
    std::map<std::int32_t, std::vector<std::pair<std::int32_t, int>>> links_;  // vertex -> (piece, side)
};

}  // namespace trainz::sim
