#include "TrackLayout.h"

#include "ByteOrder.h"

#include <cmath>
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
    for (const auto& s : t.stretches) {
        byIndex.emplace(s.index, &s);
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

}  // namespace trainz::content
