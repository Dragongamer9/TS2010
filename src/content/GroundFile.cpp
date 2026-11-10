#include "GroundFile.h"

#include "ByteOrder.h"
#include "ContentError.h"

#include <algorithm>
#include <cstring>

namespace trainz::content {

namespace {

constexpr std::uint32_t kMaxSections = 100000;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kEntrySize = 20;

}  // namespace

void overviewToRgb(std::uint16_t v, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    const unsigned lo = v & 31u;
    const unsigned mid = (v >> 5) & 63u;
    const unsigned hi = (v >> 11) & 31u;
    r = static_cast<std::uint8_t>((lo * 255 + 15) / 31);
    g = static_cast<std::uint8_t>((mid * 255 + 31) / 63);
    b = static_cast<std::uint8_t>((hi * 255 + 15) / 31);
}

GroundFile parseGround(std::span<const std::uint8_t> data) {
    if (data.size() < kHeaderSize) {
        throw ContentError("too small to be a mapfile.gnd (" + std::to_string(data.size()) + " bytes)");
    }
    if (std::memcmp(data.data(), "GND\r", 4) != 0) {
        throw ContentError("not a mapfile.gnd (no \"GND\" marker)");
    }
    GroundFile g;
    const std::uint32_t count = loadU32LE(data.data() + 4);
    g.headerWord2 = loadU32LE(data.data() + 8);
    g.headerWord3 = loadU32LE(data.data() + 12);
    if (count == 0 || count > kMaxSections ||
        static_cast<std::uint64_t>(count) * kEntrySize > data.size() - kHeaderSize) {
        throw ContentError("mapfile.gnd section count " + std::to_string(count) + " does not fit the file");
    }
    const std::size_t indexEnd = kHeaderSize + static_cast<std::size_t>(count) * kEntrySize;
    g.sections.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* e = data.data() + kHeaderSize + static_cast<std::size_t>(i) * kEntrySize;
        GroundSection s;
        s.tag = {static_cast<char>(e[3]), static_cast<char>(e[2]), static_cast<char>(e[1]), static_cast<char>(e[0])};
        s.start = loadU32LE(e + 4);
        s.end = loadU32LE(e + 8);
        s.x = static_cast<std::int32_t>(loadU32LE(e + 12));
        s.y = static_cast<std::int32_t>(loadU32LE(e + 16));
        if (s.tag != "Sec5" && s.tag != "Secs") {
            throw ContentError("section " + std::to_string(i) + " has an unknown tag");
        }
        if (s.start < indexEnd || s.end < s.start ||
            static_cast<std::uint64_t>(s.end) + kGroundOverviewBytes > data.size()) {
            throw ContentError("section " + std::to_string(i) + " (" + std::to_string(s.x) + "," + std::to_string(s.y) +
                               ") has offsets outside the file");
        }
        g.sections.push_back(std::move(s));
    }
    // Two sections at the same grid position: seen once in the disc route
    // ((-1, 0): one Sec5 and one Secs). Not an error, but worth knowing.
    {
        std::vector<std::size_t> order(g.sections.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            const auto& sa = g.sections[a];
            const auto& sb = g.sections[b];
            return sa.x != sb.x ? sa.x < sb.x : (sa.y != sb.y ? sa.y < sb.y : a < b);
        });
        for (std::size_t k = 1; k < order.size(); ++k) {
            const auto& p = g.sections[order[k - 1]];
            const auto& q = g.sections[order[k]];
            if (p.x == q.x && p.y == q.y) {
                g.warnings.push_back("sections " + std::to_string(order[k - 1]) + " (" + p.tag + ") and " +
                                     std::to_string(order[k]) + " (" + q.tag + ") are both at (" + std::to_string(p.x) +
                                     ", " + std::to_string(p.y) + ")");
            }
        }
    }
    // Sections must not overlap (each owns [start, end + 8192)).
    std::vector<const GroundSection*> byStart;
    for (const auto& s : g.sections) {
        byStart.push_back(&s);
    }
    std::sort(byStart.begin(), byStart.end(), [](const auto* a, const auto* b) { return a->start < b->start; });
    for (std::size_t i = 1; i < byStart.size(); ++i) {
        if (byStart[i]->start < static_cast<std::uint64_t>(byStart[i - 1]->end) + kGroundOverviewBytes) {
            throw ContentError("sections overlap in mapfile.gnd");
        }
        if (byStart[i]->start != byStart[i - 1]->end + kGroundOverviewBytes) {
            g.warnings.push_back("gap between sections (not seen in the sample)");
        }
    }
    const std::uint64_t lastEnd = static_cast<std::uint64_t>(byStart.back()->end) + kGroundOverviewBytes;
    if (lastEnd != data.size()) {
        g.warnings.push_back(std::to_string(data.size() - lastEnd) + " bytes after the last section");
    }
    g.extraHeader.assign(data.begin() + static_cast<std::ptrdiff_t>(indexEnd),
                         data.begin() + static_cast<std::ptrdiff_t>(byStart.front()->start));
    for (auto& s : g.sections) {
        s.data.assign(data.begin() + s.start, data.begin() + s.end);
        s.overview.resize(kGroundOverviewSide * kGroundOverviewSide);
        for (std::size_t j = 0; j < s.overview.size(); ++j) {
            s.overview[j] = loadU16LE(data.data() + s.end + 2 * j);
        }
    }
    return g;
}

}  // namespace trainz::content
