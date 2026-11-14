#include "TrackNetwork.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <map>
#include <numbers>

namespace trainz::sim {

using namespace trainz::content;

TrackNetwork::TrackNetwork(const TrackLayout& layout) {
    for (const auto& st : layout.stretches) {
        const auto c = trackCurve(layout, st);
        if (!c || pieces_.count(st.index) != 0) {
            continue;
        }
        Piece pc;
        pc.from = st.from, pc.to = st.to, pc.curve = *c;
        pc.cumulative.reserve(kSamples + 1);
        double prev[3];
        c->point(0, prev);
        double total = 0;
        pc.cumulative.push_back(0);
        for (int k = 1; k <= kSamples; ++k) {
            double q[3];
            c->point(static_cast<double>(k) / kSamples, q);
            total += std::sqrt((q[0] - prev[0]) * (q[0] - prev[0]) + (q[1] - prev[1]) * (q[1] - prev[1]) +
                               (q[2] - prev[2]) * (q[2] - prev[2]));
            pc.cumulative.push_back(total);
            std::copy(q, q + 3, prev);
        }
        if (!(total > 1e-6) || !std::isfinite(total)) {
            continue;
        }
        pieces_.emplace(st.index, std::move(pc));
    }
    for (const auto& [index, v] : layout.vertices) {
        for (const auto& l : v.links) {
            const auto it = pieces_.find(l.item);
            if (it != pieces_.end() && (it->second.from == index || it->second.to == index)) {
                links_[index].push_back({l.item, l.flag != 0 ? 1 : 0});
            }
        }
        if (const auto j = trainz::content::junctionSetting(v); j && pieces_.count(j->legs[j->selected]) != 0) {
            switches[index] = j->legs[j->selected];
        }
    }
}

std::int32_t TrackNetwork::chooseExit(std::int32_t vertex, const TrackPosition& arrivingAt,
                                      const std::vector<std::int32_t>& options) const {
    std::int32_t next = options.front();
    if (const auto sw = switches.find(vertex);
        sw != switches.end() && std::find(options.begin(), options.end(), sw->second) != options.end()) {
        return sw->second;
    }
    if (options.size() > 1) {
        // The way that turns least from the direction of arrival.
        const auto arriving = point(arrivingAt);
        double best = 1e9;
        for (const std::int32_t o : options) {
            const Piece& op = pieces_.at(o);
            TrackPosition leave{o, op.from == vertex ? 0.0 : op.cumulative.back(), op.from == vertex ? 1 : -1};
            const auto lp = point(leave);
            if (!arriving || !lp) {
                continue;
            }
            const double turn = std::fabs(std::remainder(lp->heading - arriving->heading, 2 * std::numbers::pi));
            if (turn < best) {
                best = turn, next = o;
            }
        }
    }
    return next;
}

void TrackNetwork::walk(const TrackPosition& start, double maxDistance, const std::function<bool(const Span&)>& visit) const {
    TrackPosition p = start;
    double travelled = 0;
    for (int guard = 0; guard < 100000; ++guard) {
        const auto it = pieces_.find(p.piece);
        if (it == pieces_.end()) {
            return;
        }
        const Piece& pc = it->second;
        const double len = pc.cumulative.back();
        const double offset = std::clamp(p.offset, 0.0, len);
        const bool forward = p.direction >= 0;
        const double room = forward ? len - offset : offset;
        const double left = maxDistance - travelled;
        if (!(left >= 0)) {
            return;
        }
        const double step = std::min(room, left);
        if (!visit(Span{p.piece, offset, forward ? offset + step : offset - step, forward ? 1 : -1, travelled})) {
            return;
        }
        travelled += step;
        if (room >= left) {
            return;
        }
        const std::int32_t vertex = forward ? pc.to : pc.from;
        const auto options = exits(vertex, p.piece);
        if (options.empty()) {
            return;
        }
        const std::int32_t next = chooseExit(vertex, TrackPosition{p.piece, forward ? len : 0.0, forward ? 1 : -1}, options);
        const Piece& np = pieces_.at(next);
        p = np.from == vertex ? TrackPosition{next, 0.0, +1} : TrackPosition{next, np.cumulative.back(), -1};
    }
}

std::optional<TrackNetwork::JunctionAhead> TrackNetwork::junctionAhead(const TrackPosition& start, double maxDistance) const {
    TrackPosition p = start;
    double travelled = 0;
    for (int guard = 0; guard < 10000 && travelled <= maxDistance; ++guard) {
        const auto it = pieces_.find(p.piece);
        if (it == pieces_.end()) {
            return std::nullopt;
        }
        const Piece& pc = it->second;
        const double len = pc.cumulative.back();
        const double offset = std::clamp(p.offset, 0.0, len);
        const bool forward = p.direction >= 0;
        travelled += forward ? len - offset : offset;
        if (travelled > maxDistance) {
            return std::nullopt;
        }
        const std::int32_t vertex = forward ? pc.to : pc.from;
        auto options = exits(vertex, p.piece);
        if (options.size() > 1) {
            return JunctionAhead{vertex, std::move(options), travelled};
        }
        if (options.empty()) {
            return std::nullopt;
        }
        const Piece& np = pieces_.at(options.front());
        p = np.from == vertex ? TrackPosition{options.front(), 0.0, +1} : TrackPosition{options.front(), np.cumulative.back(), -1};
    }
    return std::nullopt;
}

std::optional<TrackNetwork::Route> TrackNetwork::route(const TrackPosition& from, const TrackPosition& to, double maxDistance) const {
    const auto start = pieces_.find(from.piece);
    const auto goal = pieces_.find(to.piece);
    if (start == pieces_.end() || goal == pieces_.end()) {
        return std::nullopt;
    }
    const double startLen = start->second.cumulative.back(), goalLen = goal->second.cumulative.back();
    const double startOffset = std::clamp(from.offset, 0.0, startLen), goalOffset = std::clamp(to.offset, 0.0, goalLen);
    const int startDir = from.direction >= 0 ? 1 : -1;
    // Already on the target piece, ahead of us.
    if (from.piece == to.piece && (goalOffset - startOffset) * startDir >= 0) {
        return Route{std::fabs(goalOffset - startOffset), {}};
    }
    // Dijkstra over (piece, direction) states, each reached at the vertex it is entered by.
    using State = std::pair<std::int32_t, int>;
    struct Came {
        State prev;
        std::int32_t vertex = -1;
        bool choice = false;
    };
    std::map<State, double> best;
    std::map<State, Came> came;
    std::priority_queue<std::pair<double, State>, std::vector<std::pair<double, State>>, std::greater<>> open;
    const double toEnd = startDir > 0 ? startLen - startOffset : startOffset;
    best[{from.piece, startDir}] = 0;  // the start state (its far end is toEnd away)
    open.push({0.0, {from.piece, startDir}});
    std::optional<Route> found;
    std::optional<State> foundState;
    while (!open.empty()) {
        const auto [d, st] = open.top();
        open.pop();
        if (d > best[st] + 1e-9 || (found && d >= found->distance)) {
            continue;
        }
        const Piece& pc = pieces_.at(st.first);
        // Distance from the search start to this state's far end.
        const double atEnd = st == State{from.piece, startDir} ? toEnd : d + pc.cumulative.back();
        if (atEnd > maxDistance) {
            continue;
        }
        const std::int32_t vertex = st.second > 0 ? pc.to : pc.from;
        const auto options = exits(vertex, st.first);
        for (const std::int32_t next : options) {
            const Piece& np = pieces_.at(next);
            const int dir = np.from == vertex ? 1 : -1;
            const State ns{next, dir};
            if (next == to.piece) {  // arrives on the target piece
                const double total = atEnd + (dir > 0 ? goalOffset : np.cumulative.back() - goalOffset);
                if (!found || total < found->distance) {
                    found = Route{total, {}};
                    foundState = ns;
                    came[ns] = {st, vertex, options.size() > 1};
                }
            }
            const auto it = best.find(ns);
            if (it == best.end() || atEnd < it->second) {
                best[ns] = atEnd;
                if (next != to.piece) {
                    came[ns] = {st, vertex, options.size() > 1};
                }
                open.push({atEnd, ns});
            }
        }
    }
    if (!found) {
        return std::nullopt;
    }
    // The junction settings, from the start onward.
    for (State st = *foundState; came.count(st) != 0 && st != State{from.piece, startDir};) {
        const Came& c = came.at(st);
        if (c.choice) {
            found->switches.push_back({c.vertex, st.first});
        }
        st = c.prev;
    }
    std::reverse(found->switches.begin(), found->switches.end());
    return found;
}

std::int32_t TrackNetwork::toggle(const JunctionAhead& j) {
    if (j.ways.empty()) {
        return -1;
    }
    const auto sw = switches.find(j.vertex);
    auto at = sw == switches.end() ? j.ways.end() : std::find(j.ways.begin(), j.ways.end(), sw->second);
    const std::int32_t next = at == j.ways.end() || at + 1 == j.ways.end() ? j.ways.front() : *(at + 1);
    switches[j.vertex] = next;
    return next;
}

std::optional<double> TrackNetwork::length(std::int32_t piece) const {
    const auto it = pieces_.find(piece);
    if (it == pieces_.end()) {
        return std::nullopt;
    }
    return it->second.cumulative.back();
}

double TrackNetwork::paramAt(const Piece& pc, double offset) const {
    const auto& c = pc.cumulative;
    offset = std::clamp(offset, 0.0, c.back());
    const auto it = std::upper_bound(c.begin(), c.end(), offset);
    const std::size_t k = std::min<std::size_t>(c.size() - 1, static_cast<std::size_t>(std::max<std::ptrdiff_t>(1, it - c.begin())));
    const double a = c[k - 1], b = c[k];
    const double f = b > a ? (offset - a) / (b - a) : 0;
    return (static_cast<double>(k - 1) + f) / kSamples;
}

std::optional<TrackPoint> TrackNetwork::point(const TrackPosition& p) const {
    const auto it = pieces_.find(p.piece);
    if (it == pieces_.end() || !std::isfinite(p.offset)) {
        return std::nullopt;
    }
    const Piece& pc = it->second;
    const double s = paramAt(pc, p.offset);
    double q[3], a[3], b[3];
    pc.curve.point(s, q);
    const double e = 0.5 / kSamples;
    pc.curve.point(std::max(0.0, s - e), a);
    pc.curve.point(std::min(1.0, s + e), b);
    const double run = std::hypot(b[0] - a[0], b[1] - a[1]);
    const double along = std::sqrt(run * run + (b[2] - a[2]) * (b[2] - a[2]));
    TrackPoint t;
    t.x = q[0], t.y = q[1], t.z = q[2];
    const int d = p.direction >= 0 ? 1 : -1;
    t.heading = std::atan2(d * (b[1] - a[1]), d * (b[0] - a[0]));
    t.grade = along > 1e-9 ? d * (b[2] - a[2]) / along : 0;
    return t;
}

std::optional<TrackPosition> TrackNetwork::locate(double x, double y, double maxDistance) const {
    if (!std::isfinite(x) || !std::isfinite(y) || !(maxDistance > 0)) {
        return std::nullopt;
    }
    std::optional<TrackPosition> best;
    double bestD = maxDistance;
    for (const auto& [index, pc] : pieces_) {
        double prev[3];
        pc.curve.point(0, prev);
        for (int k = 1; k <= kSamples; ++k) {
            double q[3];
            pc.curve.point(static_cast<double>(k) / kSamples, q);
            const double dx = q[0] - prev[0], dy = q[1] - prev[1], l2 = dx * dx + dy * dy;
            const double f = l2 > 0 ? std::clamp(((x - prev[0]) * dx + (y - prev[1]) * dy) / l2, 0.0, 1.0) : 0.0;
            const double d = std::hypot(x - (prev[0] + f * dx), y - (prev[1] + f * dy));
            if (d < bestD) {
                bestD = d;
                const double a = pc.cumulative[static_cast<std::size_t>(k - 1)], b = pc.cumulative[static_cast<std::size_t>(k)];
                best = TrackPosition{index, a + f * (b - a), +1};
            }
            std::copy(q, q + 3, prev);
        }
    }
    return best;
}

std::vector<std::int32_t> TrackNetwork::exits(std::int32_t vertex, std::int32_t arrivingPiece) const {
    std::vector<std::int32_t> out;
    const auto it = links_.find(vertex);
    if (it == links_.end()) {
        return out;
    }
    int side = -1;
    for (const auto& [piece, s] : it->second) {
        if (piece == arrivingPiece) {
            side = s;
        }
    }
    for (const auto& [piece, s] : it->second) {
        if (piece != arrivingPiece && (side < 0 || s != side)) {
            out.push_back(piece);
        }
    }
    return out;
}

TrackNetwork::Move TrackNetwork::advance(TrackPosition& p, double metres) const {
    Move m;
    if (!std::isfinite(metres) || metres == 0 || pieces_.count(p.piece) == 0) {
        return m;
    }
    const int sign = metres > 0 ? 1 : -1;
    double left = std::fabs(metres);
    for (int guard = 0; guard < 100000 && left > 0; ++guard) {
        const Piece& pc = pieces_.at(p.piece);
        const double len = pc.cumulative.back();
        p.offset = std::clamp(p.offset, 0.0, len);
        const int along = (p.direction >= 0 ? 1 : -1) * sign;  // +1: offset grows
        const double room = along > 0 ? len - p.offset : p.offset;
        if (left <= room) {
            p.offset += along * left;
            m.moved += left;
            left = 0;
            break;
        }
        // To the end of this piece, then on through the vertex.
        p.offset = along > 0 ? len : 0;
        m.moved += room;
        left -= room;
        const std::int32_t vertex = along > 0 ? pc.to : pc.from;
        auto options = exits(vertex, p.piece);
        if (options.empty()) {
            m.stoppedAtEnd = true;
            break;
        }
        TrackPosition here = p;
        here.direction = along;
        const std::int32_t next = chooseExit(vertex, here, options);
        const Piece& np = pieces_.at(next);
        const int newAlong = np.from == vertex ? 1 : -1;
        p.piece = next;
        p.offset = newAlong > 0 ? 0 : np.cumulative.back();
        p.direction = newAlong * sign;
    }
    m.moved *= sign;
    return m;
}

}  // namespace trainz::sim
