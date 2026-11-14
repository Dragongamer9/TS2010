#include "GroundFile.h"

#include "ByteOrder.h"
#include "ContentError.h"

#include <algorithm>
#include <cstring>

namespace trainz::content {

namespace {

constexpr std::uint32_t kMaxSections = 100000;
constexpr std::size_t kHeaderSize = 8;   // "GND", version, count
constexpr std::size_t kEntrySize = 20;   // x, y, tag, start, end (version >= 12)

// Sequential reader over one section's bytes. Unlike the game's stream
// (which substitutes 0 and sets an error flag), running out is an error.
class SectionReader {
public:
    SectionReader(std::span<const std::uint8_t> d) : d_(d) {}

    std::uint8_t u8() {
        need(1);
        return d_[pos_++];
    }
    std::uint16_t u16() {
        need(2);
        const auto v = loadU16LE(d_.data() + pos_);
        pos_ += 2;
        return v;
    }
    std::uint32_t u32() {
        need(4);
        const auto v = loadU32LE(d_.data() + pos_);
        pos_ += 4;
        return v;
    }
    float f32() {
        const std::uint32_t u = u32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::int64_t i64() {
        const std::uint64_t lo = u32();
        const std::uint64_t hi = u32();
        return static_cast<std::int64_t>(lo | (hi << 32));
    }
    // KUID as stored by 0x80c120 / read by 0x80c8d0: u32 content, u32 packed user/version.
    Kuid kuid() {
        const auto content = static_cast<std::int32_t>(u32());
        return unpackChumpKuid(u32(), content);
    }
    std::vector<std::uint8_t> bytes(std::size_t n) {
        need(n);
        std::vector<std::uint8_t> v(d_.begin() + static_cast<std::ptrdiff_t>(pos_),
                                    d_.begin() + static_cast<std::ptrdiff_t>(pos_ + n));
        pos_ += n;
        return v;
    }
    std::size_t pos() const { return pos_; }
    std::size_t size() const { return d_.size(); }

private:
    void need(std::size_t n) const {
        if (d_.size() - pos_ < n) {
            throw ContentError("data ends early at byte " + std::to_string(pos_) + " of " +
                               std::to_string(d_.size()));
        }
    }
    std::span<const std::uint8_t> d_;
    std::size_t pos_ = 0;
};

// 0x6f0e50 (version = the file's version byte).
void readVertex(SectionReader& r, std::uint8_t version, GroundVertex& v) {
    const std::uint8_t first = r.u8();
    if (first == 0xFE || first == 0xFD) {
        // Single texture at full weight; 0xFE: height 0, 0xFD: height follows.
        v.texture[0] = r.u8();
        v.weight[0] = 0xFF;
        if (version > 2) {
            v.extra[0] = r.u8();
        }
        v.height = first == 0xFD ? r.f32() : 0.0f;
        v.layersInFile = 1;
    } else {
        // List of (texture, weight, [extra]) ended by a 0xFF texture byte.
        // The game keeps the first 3 and stops after 33 entries (0x6f10fc).
        std::uint8_t tex = first;
        unsigned n = 0;
        while (tex != 0xFF) {
            const std::uint8_t w = r.u8();
            const std::uint8_t e = version > 2 ? r.u8() : 0;
            if (n < 3) {
                v.texture[n] = tex;
                v.weight[n] = w;
                v.extra[n] = e;
            }
            tex = r.u8();
            ++n;
            if (n > 0x20) {
                break;
            }
        }
        v.layersInFile = static_cast<std::uint8_t>(std::min(n, 255u));
        v.height = r.f32();
    }
    if (version < 12) {
        (void)r.u8();  // three more bytes (0x6f11af..0x6f11ce); not kept here
        (void)r.u8();
        (void)r.u8();
    }
}

}  // namespace

std::size_t groundVertexSide(const std::string& tag) {
    return tag == "Sec5" ? 148 : 76;
}

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
    // The game reads but does not compare these three bytes (0x6d2bf2..0x6d2c0c).
    if (std::memcmp(data.data(), "GND", 3) != 0) {
        throw ContentError("not a mapfile.gnd (no \"GND\" marker)");
    }
    GroundFile g;
    g.version = data[3];
    // The game accepts 1..13 (0x6d2c2a). The overview map of versions < 12 is
    // a different size (0x6e43a6) and has not been worked out; no such file
    // is named mapfile.gnd on the disc.
    if (g.version < 12 || g.version > 13) {
        throw ContentError("mapfile.gnd version " + std::to_string(g.version) + " is not supported (12 and 13 are)");
    }
    const std::uint32_t count = loadU32LE(data.data() + 4);
    if (count == 0 || count > kMaxSections ||
        static_cast<std::uint64_t>(count) * kEntrySize > data.size() - kHeaderSize) {
        throw ContentError("mapfile.gnd section count " + std::to_string(count) + " does not fit the file");
    }
    const std::size_t indexEnd = kHeaderSize + static_cast<std::size_t>(count) * kEntrySize;
    g.sections.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* e = data.data() + kHeaderSize + static_cast<std::size_t>(i) * kEntrySize;
        GroundSection s;
        // 0x6d2c4f..0x6d2c80: x and y, then the tag; 0x827230: start and end.
        s.x = static_cast<std::int32_t>(loadU32LE(e));
        s.y = static_cast<std::int32_t>(loadU32LE(e + 4));
        s.tag = {static_cast<char>(e[11]), static_cast<char>(e[10]), static_cast<char>(e[9]), static_cast<char>(e[8])};
        s.start = loadU32LE(e + 12);
        s.end = loadU32LE(e + 16);
        // The game builds Sec5 for 'Sec5' and Secs for anything else (0x6dd62c).
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
            g.warnings.push_back("gap between sections (not seen on the disc)");
        }
    }
    const std::uint64_t lastEnd = static_cast<std::uint64_t>(byStart.back()->end) + kGroundOverviewBytes;
    if (lastEnd != data.size()) {
        g.warnings.push_back(std::to_string(data.size() - lastEnd) + " bytes after the last section");
    }
    g.extraHeader.assign(data.begin() + static_cast<std::ptrdiff_t>(indexEnd),
                         data.begin() + static_cast<std::ptrdiff_t>(byStart.front()->start));
    try {
        g.world = parseGroundWorld(g.extraHeader, g.version);
        g.worldDecoded = true;
    } catch (const ContentError& err) {
        g.warnings.push_back(std::string("world data not decoded: ") + err.what());
    }
    for (auto& s : g.sections) {
        s.data.assign(data.begin() + s.start, data.begin() + s.end);
        s.overview.resize(kGroundOverviewSide * kGroundOverviewSide);
        for (std::size_t j = 0; j < s.overview.size(); ++j) {
            s.overview[j] = loadU16LE(data.data() + s.end + 2 * j);
        }
    }
    return g;
}

GroundWorld parseGroundWorld(std::span<const std::uint8_t> worldData, std::uint8_t version) {
    SectionReader r(worldData);
    GroundWorld w;
    if (version < 7) {
        // 0x463cb0 reads the older environment block; not traced.
        throw ContentError("world data of version " + std::to_string(version) + " is not handled");
    }
    // Environment, 0x463dd0. A first word of -1 announces a sub-version,
    // after which the key count follows (unless the sub-version is -1 too).
    std::int32_t count = r.i32();
    if (count == -1) {
        w.envSubVersion = r.i32();
        if (w.envSubVersion != -1) {
            count = r.i32();
        }
    }
    count = std::clamp(count, 0, 32);  // 0x463e1c..0x463e42
    w.envKuid = r.kuid();
    w.envFloats[0] = r.f32();
    w.envFloats[1] = r.f32();
    w.envKeys.resize(static_cast<std::size_t>(count));
    for (auto& k : w.envKeys) {  // 0x463f1a..0x463fce
        k.time = r.f32();
        for (auto& c : k.colourA) {
            for (float& f : c) {
                f = r.f32();
            }
        }
        k.time2 = r.f32();
        for (auto& c : k.colourB) {
            for (float& f : c) {
                f = r.f32();
            }
        }
    }
    if (w.envSubVersion != -1) {
        w.hasDate = true;
        w.date = r.i64();  // 0x463ff5
        if (w.envSubVersion >= 1) {
            w.hasEnvExtra = true;
            w.envExtra[0] = r.f32();
            w.envExtra[1] = r.f32();
        }
    }
    // Jump table on version at 0x6d2d6c: 11..13 -> 0x6d2d73, 9..10 -> 0x6d2d7c, 8 -> 0x6d2da5.
    if (version >= 11) {
        for (float& f : w.colour) {
            f = r.f32();
        }
    }
    if (version >= 9) {
        w.kuid = r.kuid();
    }
    if (version >= 8) {
        // Object container, 0x886be0 (called from 0x6da060 with flags 1, 0).
        // Object container (0x886be0), shared with mapfile.obs / .trk: see ObjectContainer.h.
        std::size_t cpos = r.pos();
        ObjectContainer c = readObjectContainer(worldData, cpos);
        (void)r.bytes(cpos - r.pos());
        w.containerVersion = c.version;
        w.containerWord = c.word;
        w.items = std::move(c.items);
        w.containerTail = std::move(c.tail);
    }
    if (version < 13) {
        // Ground-texture table, 0x6d2db8..0x6d2ef8: at most 250 entries (0x1f40 / 0x20).
        const std::int32_t n = std::clamp(r.i32(), 0, 250);
        for (std::int32_t i = 0; i < n; ++i) {
            const float weight = r.f32();
            w.textureTable.emplace_back(weight, r.kuid());
        }
    }
    if (r.pos() != r.size()) {
        throw ContentError("world data has " + std::to_string(r.size() - r.pos()) +
                           " bytes left over before the first section");
    }
    return w;
}

GroundSectionData parseGroundSection(const GroundSection& section, std::uint8_t version) {
    SectionReader r(section.data);
    GroundSectionData out;
    // Texture slots (version >= 13 only; 0x82be6a).
    if (version >= 13) {
        out.textureSlotCount = r.u32();
        const std::size_t n = std::min<std::size_t>(out.textureSlotCount, kGroundTextureSlots);
        out.textures.resize(n);
        for (auto& t : out.textures) {
            t.weight = r.f32();
            if (t.weight > 0.0f) {  // NaN and <= 0: no KUID
                const auto content = static_cast<std::int32_t>(r.u32());
                const std::uint32_t packed = r.u32();
                t.used = true;
                t.kuid = unpackChumpKuid(packed, content);
            }
        }
    }
    // Height cells (version >= 4; 0x82bfeb). A count above 36 * 36 is
    // replaced by 0 and nothing more is read for this block (0x82c038).
    if (version >= 4) {
        std::uint32_t n = r.u32();
        if (n > kGroundCellSide * kGroundCellSide) {
            n = 0;
        }
        out.cells.resize(n);
        for (auto& c : out.cells) {
            if (version >= 5) {
                c.col = r.u16();
                c.row = r.u16();
            } else {
                // 0x82c150..0x82c1c8: one index into the 76-wide vertex grid.
                const auto idx = static_cast<std::int32_t>(r.u32());
                const std::int32_t side = static_cast<std::int32_t>(groundVertexSide(section.tag));
                c.col = static_cast<std::uint16_t>((idx % side) / 2);
                c.row = static_cast<std::uint16_t>((idx / side) / 2);
            }
            c.value = r.f32();
        }
    }
    const std::size_t side = groundVertexSide(section.tag);
    out.vertices.resize(side * side);
    for (auto& v : out.vertices) {
        readVertex(r, version, v);
    }
    if (r.pos() != r.size()) {
        throw ContentError("section at (" + std::to_string(section.x) + ", " + std::to_string(section.y) + ") has " +
                           std::to_string(r.size() - r.pos()) + " bytes left over after its vertices");
    }
    return out;
}

}  // namespace trainz::content
