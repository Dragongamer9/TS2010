#include "TrackLayout.h"

#include "ByteOrder.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <cstring>

namespace trainz::content {

namespace {

float f32At(const std::uint8_t* p) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
}

}  // namespace

std::optional<TrackVertex> decodeTrackVertex(const ObjectItem& item) {
    if (item.tag != "tkVx" || item.payload.size() < 20) {
        return std::nullopt;
    }
    const std::uint8_t* p = item.payload.data();
    TrackVertex v;
    v.index = item.index;
    v.version = loadU32LE(p);
    v.boardX = static_cast<std::int16_t>(loadU16LE(p + 4));
    v.boardY = static_cast<std::int16_t>(loadU16LE(p + 6));
    v.x = f32At(p + 8);
    v.y = f32At(p + 12);
    v.z = f32At(p + 16);
    std::size_t pos = 20;
    if (item.payload.size() >= 41) {
        for (int k = 0; k < 4; ++k, pos += 5) {
            const auto linked = static_cast<std::int32_t>(loadU32LE(p + pos));
            if (linked != -1) {
                v.links.push_back({linked, p[pos + 4]});
            }
        }
        v.lastByte = p[pos++];
    }
    v.rest.assign(item.payload.begin() + static_cast<std::ptrdiff_t>(pos), item.payload.end());
    return v;
}

std::optional<TrackStretch> decodeTrackStretch(const ObjectItem& item) {
    // tkBr wraps a whole stretch after one word of its own (2 on the disc).
    const std::size_t start = item.tag == "tkBr" ? 4 : 0;
    if ((item.tag != "tkSt" && item.tag != "tkBr") || item.payload.size() < start + 20) {
        return std::nullopt;
    }
    const std::uint8_t* p = item.payload.data() + start;
    TrackStretch s;
    s.kind = item.tag;
    if (start != 0) {
        s.wrapperVersion = loadU32LE(item.payload.data());
    }
    s.index = item.index;
    s.version = loadU32LE(p);
    s.from = static_cast<std::int32_t>(loadU32LE(p + 4));
    s.to = static_cast<std::int32_t>(loadU32LE(p + 8));
    s.track = unpackChumpKuid(loadU32LE(p + 16), static_cast<std::int32_t>(loadU32LE(p + 12)));
    s.rest.assign(item.payload.begin() + static_cast<std::ptrdiff_t>(start + 20), item.payload.end());
    return s;
}

TrackLayout readTrackLayout(const RouteObjectFile& trk) {
    TrackLayout t;
    for (const auto& c : trk.containers) {
        for (const auto& item : c.items) {
            if (item.tag == "tkVx") {
                auto v = decodeTrackVertex(item);
                if (!v) {
                    t.warnings.push_back("vertex " + std::to_string(item.index) + " is too short to read");
                } else if (!std::isfinite(v->x) || !std::isfinite(v->y) || !std::isfinite(v->z)) {
                    t.warnings.push_back("vertex " + std::to_string(item.index) + " has a non-finite position");
                } else if (!t.vertices.emplace(v->index, std::move(*v)).second) {
                    t.warnings.push_back("vertex index " + std::to_string(item.index) + " appears twice; first kept");
                }
            } else if (item.tag == "tkSt" || item.tag == "tkBr") {
                auto s = decodeTrackStretch(item);
                if (s) {
                    t.stretches.push_back(std::move(*s));
                } else {
                    t.warnings.push_back("stretch " + std::to_string(item.index) + " is too short to read");
                }
            } else {
                ++t.otherItems;
            }
        }
    }
    std::map<std::int32_t, const TrackStretch*> byIndex;
    for (std::size_t i = 0; i < t.stretches.size(); ++i) {
        byIndex.emplace(t.stretches[i].index, &t.stretches[i]);
        t.stretchAt.emplace(t.stretches[i].index, i);
    }
    for (const auto& [index, v] : t.vertices) {
        for (const auto& l : v.links) {
            const auto it = byIndex.find(l.item);
            if (it == byIndex.end()) {
                t.warnings.push_back("vertex " + std::to_string(index) + " links to track piece " +
                                     std::to_string(l.item) + ", which is not in the file");
            } else if (it->second->from != index && it->second->to != index) {
                t.warnings.push_back("vertex " + std::to_string(index) + " links to track piece " +
                                     std::to_string(l.item) + ", which does not end there");
            }
        }
    }
    for (const auto& s : t.stretches) {
        if (t.vertices.count(s.from) == 0 || t.vertices.count(s.to) == 0) {
            t.warnings.push_back("stretch " + std::to_string(s.index) + " joins vertex " + std::to_string(s.from) +
                                 " and " + std::to_string(s.to) + ", but one of them is missing");
        }
    }
    return t;
}

const TrackStretch* TrackLayout::findStretch(std::int32_t index) const {
    if (const auto it = stretchAt.find(index); it != stretchAt.end() && it->second < stretches.size() &&
                                               stretches[it->second].index == index) {
        return &stretches[it->second];
    }
    if (!stretchAt.empty()) {
        return nullptr;  // indexed and not there (scanning would make missing links slow)
    }
    for (const auto& s : stretches) {
        if (s.index == index) {
            return &s;
        }
    }
    return nullptr;
}

namespace {

// Unit x/y direction from vertex v along the piece `item`, or nullopt (also when the piece
// does not end at v: such links are kept by readTrackLayout as warnings, not used here).
std::optional<std::pair<double, double>> towardOtherEnd(const TrackLayout& layout, const TrackVertex& v,
                                                        std::int32_t item) {
    if (const TrackStretch* found = layout.findStretch(item)) {
        const TrackStretch& st = *found;
        if (st.from != v.index && st.to != v.index) {
            return std::nullopt;
        }
        const std::int32_t otherIndex = st.from == v.index ? st.to : st.from;
        const auto o = layout.vertices.find(otherIndex);
        if (o == layout.vertices.end()) {
            return std::nullopt;
        }
        const double dx = o->second.worldX() - v.worldX(), dy = o->second.worldY() - v.worldY();
        const double len = std::hypot(dx, dy);
        if (!(len > 1e-6)) {
            return std::nullopt;
        }
        return std::pair{dx / len, dy / len};
    }
    return std::nullopt;
}

// The direction a piece leaves vertex v in (unit, x/y): the vertex tangent on the piece's
// side, or the chord direction `chord` when the vertex has no pieces on the other side.
std::pair<double, double> leaving(const TrackLayout& layout, const TrackVertex& v, std::int32_t item,
                                  std::pair<double, double> chord) {
    int side = -1;
    for (const auto& l : v.links) {
        if (l.item == item) {
            side = l.flag != 0 ? 1 : 0;
        }
    }
    double sx[2] = {0, 0}, sy[2] = {0, 0};
    int n[2] = {0, 0};
    for (const auto& l : v.links) {
        const int s = l.flag != 0 ? 1 : 0;
        if (const auto d = l.item == item ? std::optional(chord) : towardOtherEnd(layout, v, l.item)) {
            sx[s] += d->first, sy[s] += d->second, ++n[s];
        }
    }
    if (side < 0 || n[0] == 0 || n[1] == 0) {
        return chord;
    }
    // Next to a straight piece on the other side: carry straight on from it.
    for (const auto& l : v.links) {
        const int s = l.flag != 0 ? 1 : 0;
        const TrackStretch* p = l.item == item ? nullptr : layout.findStretch(l.item);
        if (s != side && p != nullptr && p->straight()) {
            if (const auto d = towardOtherEnd(layout, v, l.item)) {
                return {-d->first, -d->second};
            }
        }
    }
    // From side 0 toward side 1.
    double tx = sx[1] / n[1] - sx[0] / n[0], ty = sy[1] / n[1] - sy[0] / n[0];
    const double len = std::hypot(tx, ty);
    if (!(len > 1e-9)) {
        return chord;
    }
    tx /= len, ty /= len;
    return side == 1 ? std::pair{tx, ty} : std::pair{-tx, -ty};
}

}  // namespace

void TrackCurve::point(double s, double out[3]) const {
    const double s2 = s * s, s3 = s2 * s;
    const double h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
    for (int i = 0; i < 2; ++i) {
        out[i] = h00 * p0[i] + h10 * t0[i] + h01 * p1[i] + h11 * t1[i];
    }
    out[2] = p0[2] + (p1[2] - p0[2]) * s;
}

double TrackCurve::bulge() const {
    const double cx = p1[0] - p0[0], cy = p1[1] - p0[1];
    const double len = std::hypot(cx, cy);
    double worst = 0;
    for (int k = 1; k < 16; ++k) {
        double q[3];
        point(k / 16.0, q);
        worst = std::max(worst, std::fabs((q[0] - p0[0]) * cy - (q[1] - p0[1]) * cx) / len);
    }
    return worst;
}

std::optional<TrackCurve> trackCurve(const TrackLayout& layout, const TrackStretch& stretch, double tangentScale) {
    const auto a = layout.vertices.find(stretch.from), b = layout.vertices.find(stretch.to);
    if (a == layout.vertices.end() || b == layout.vertices.end()) {
        return std::nullopt;
    }
    const TrackVertex& va = a->second;
    const TrackVertex& vb = b->second;
    TrackCurve c;
    c.p0[0] = va.worldX(), c.p0[1] = va.worldY(), c.p0[2] = va.z;
    c.p1[0] = vb.worldX(), c.p1[1] = vb.worldY(), c.p1[2] = vb.z;
    const double dx = c.p1[0] - c.p0[0], dy = c.p1[1] - c.p0[1];
    const double len = std::hypot(dx, dy);
    if (!(len > 1e-3) || !std::isfinite(len)) {
        return std::nullopt;
    }
    if (stretch.straight()) {
        c.t0[0] = c.t1[0] = dx, c.t0[1] = c.t1[1] = dy;  // a Hermite curve with chord tangents is the straight line
        return c;
    }
    const auto ta = leaving(layout, va, stretch.index, {dx / len, dy / len});
    const auto tb = leaving(layout, vb, stretch.index, {-dx / len, -dy / len});
    const double m = tangentScale * len;
    c.t0[0] = ta.first * m, c.t0[1] = ta.second * m;
    c.t1[0] = -tb.first * m, c.t1[1] = -tb.second * m;  // arriving at p1 = opposite of leaving it
    return c;
}

TrackLocator::TrackLocator(const TrackLayout& layout, double centreX, double centreY, double radius, double step) {
    if (!(step > 0.1) || !std::isfinite(step)) {
        step = 2.0;
    }
    auto cell = [](double v) { return static_cast<std::int64_t>(std::floor(v / kCell)); };
    for (const auto& st : layout.stretches) {
        const auto c = trackCurve(layout, st);
        if (!c) {
            continue;
        }
        const double len = std::hypot(c->p1[0] - c->p0[0], c->p1[1] - c->p0[1]);
        // Both ends far outside the area (pieces are shorter than a few km): skip.
        if (std::hypot(c->p0[0] - centreX, c->p0[1] - centreY) > radius + len &&
            std::hypot(c->p1[0] - centreX, c->p1[1] - centreY) > radius + len) {
            continue;
        }
        // Straight pieces in 20 m segments (so each fits a few grid cells), curves every `step`.
        const int n = std::clamp(static_cast<int>(std::ceil(len / (st.straight() ? 20.0 : step))), 1, 4096);
        double prev[3];
        c->point(0, prev);
        for (int k = 1; k <= n; ++k) {
            Seg sg{};
            std::copy(prev, prev + 3, sg.a);
            c->point(static_cast<double>(k) / n, sg.b);
            std::copy(sg.b, sg.b + 3, prev);
            sg.piece = st.index;
            if (!(std::hypot(sg.b[0] - sg.a[0], sg.b[1] - sg.a[1]) > 1e-6)) {
                continue;  // no length: no direction
            }
            const std::size_t i = segs_.size();
            segs_.push_back(sg);
            const auto x0 = cell(std::min(sg.a[0], sg.b[0])), x1 = cell(std::max(sg.a[0], sg.b[0]));
            const auto y0 = cell(std::min(sg.a[1], sg.b[1])), y1 = cell(std::max(sg.a[1], sg.b[1]));
            if ((x1 - x0 + 1) * (y1 - y0 + 1) > 64) {
                continue;  // cannot happen for segments of at most ~20 m; a guard against bad data
            }
            for (auto gx = x0; gx <= x1; ++gx) {
                for (auto gy = y0; gy <= y1; ++gy) {
                    grid_[{gx, gy}].push_back(i);
                }
            }
        }
    }
}

std::optional<TrackSample> TrackLocator::nearest(double x, double y, double maxDistance) const {
    if (!std::isfinite(x) || !std::isfinite(y) || !(maxDistance > 0)) {
        return std::nullopt;
    }
    maxDistance = std::min(maxDistance, kCell);
    const auto cx = static_cast<std::int64_t>(std::floor(x / kCell)), cy = static_cast<std::int64_t>(std::floor(y / kCell));
    std::optional<TrackSample> best;
    for (std::int64_t gx = cx - 1; gx <= cx + 1; ++gx) {
        for (std::int64_t gy = cy - 1; gy <= cy + 1; ++gy) {
            const auto it = grid_.find({gx, gy});
            if (it == grid_.end()) {
                continue;
            }
            for (const std::size_t i : it->second) {
                const Seg& sg = segs_[i];
                const double dx = sg.b[0] - sg.a[0], dy = sg.b[1] - sg.a[1], l2 = dx * dx + dy * dy;
                const double f = l2 > 0 ? std::clamp(((x - sg.a[0]) * dx + (y - sg.a[1]) * dy) / l2, 0.0, 1.0) : 0.0;
                const double px = sg.a[0] + f * dx, py = sg.a[1] + f * dy;
                const double d = std::hypot(x - px, y - py);
                if (d <= maxDistance && (!best || d < best->distance)) {
                    best = TrackSample{px, py, sg.a[2] + f * (sg.b[2] - sg.a[2]), std::atan2(dy, dx), d, sg.piece};
                }
            }
        }
    }
    return best;
}

}  // namespace trainz::content
