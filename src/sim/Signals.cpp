#include "Signals.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace trainz::sim {

const char* aspectName(Aspect a) {
    switch (a) {
    case Aspect::Stop:
        return "stop";
    case Aspect::Caution:
        return "caution";
    case Aspect::Clear:
        return "clear";
    }
    return "?";
}

SignalSystem::SignalSystem(const TrackNetwork& net, std::vector<Signal> signals)
    : net_(net), signals_(std::move(signals)), aspects_(signals_.size(), Aspect::Clear) {
    for (std::size_t i = 0; i < signals_.size(); ++i) {
        byPiece_.emplace(signals_[i].at.piece, i);
    }
}

std::optional<SignalSystem::Ahead> SignalSystem::firstAhead(const TrackPosition& start, double maxDistance) const {
    std::optional<Ahead> found;
    net_.walk(start, maxDistance, [&](const TrackNetwork::Span& s) {
        const auto [b, e] = byPiece_.equal_range(s.piece);
        for (auto it = b; it != e; ++it) {
            const TrackPosition& at = signals_[it->second].at;
            if ((at.direction >= 0 ? 1 : -1) != s.direction) {
                continue;  // for the other direction
            }
            const double along = (at.offset - s.from) * s.direction;  // metres past the span's entry
            const double span = std::fabs(s.to - s.from);
            if (along < 0 || along > span) {
                continue;
            }
            const double d = s.startDistance + along;
            if (d > 0.01 && (!found || d < found->distance)) {
                found = Ahead{it->second, d};
            }
        }
        return !found;  // the first span holding one has the nearest
    });
    return found;
}

std::optional<SignalSystem::Ahead> SignalSystem::nextSignal(const TrackPosition& p, double maxDistance) const {
    return firstAhead(p, maxDistance);
}

void SignalSystem::update(const std::vector<Occupied>& occupied) {
    std::multimap<std::int32_t, double> taken;
    for (const auto& o : occupied) {
        taken.emplace(o.piece, o.offset);
    }
    std::vector<std::optional<std::size_t>> next(signals_.size());
    for (std::size_t i = 0; i < signals_.size(); ++i) {
        const auto ahead = firstAhead(signals_[i].at, kMaxBlock);
        const double blockEnd = ahead ? ahead->distance : kMaxBlock;
        bool busy = false;
        net_.walk(signals_[i].at, blockEnd, [&](const TrackNetwork::Span& s) {
            const auto [b, e] = taken.equal_range(s.piece);
            const double lo = std::min(s.from, s.to), hi = std::max(s.from, s.to);
            for (auto it = b; it != e && !busy; ++it) {
                busy = it->second >= lo && it->second <= hi;
            }
            return !busy;
        });
        aspects_[i] = busy ? Aspect::Stop : Aspect::Clear;
        next[i] = ahead ? std::optional<std::size_t>(ahead->index) : std::nullopt;
    }
    for (std::size_t i = 0; i < signals_.size(); ++i) {
        if (aspects_[i] == Aspect::Clear && next[i] && aspects_[*next[i]] == Aspect::Stop) {
            aspects_[i] = Aspect::Caution;
        }
    }
}

}  // namespace trainz::sim
