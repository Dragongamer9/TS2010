// Track geometry from mapfile.trk: vertices (tkVx) and the stretches (tkSt)
// joining them. See docs/formats/route-objects.md.
//
// OBSERVED, not traced (the classes' own readers are not traced yet):
// - tkVx: u32 version (3 on all 406,207 disc vertices), i16 boardX, i16 boardY,
//   f32 x, f32 y, f32 z, then 4 link slots {i32 item (-1 = empty), u8 flag} and
//   one more byte. The slots list every track piece (tkSt or tkBr) that ends at
//   the vertex: 587,479 of 587,500 disc stretch ends are listed. The flag's
//   meaning is unknown (it is not simply "from" or "to"). x/y are metres inside a
//   720 m baseboard and z is the height in metres: on the disc, 99.5% of
//   vertices fall inside a terrain section and 66% lie within 1 m of the ground
//   there (with rows flipped instead: 39%).
// - tkSt: u32 version (6 or 4), i32 from, i32 to (vertex indices), then the
//   track asset's KUID (always equal to the item's own), then 4..10 bytes not
//   decoded (no floats: how a stretch curves is not stored there). Both ends exist
//   on all 293,750 disc stretches. tkBr (88/94 bytes) is u32 2 followed by a
//   whole stretch, then more fields not decoded; its ends are the vertices that
//   link to it.
#pragma once

#include "content/Kuid.h"
#include "content/ObjectContainer.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace trainz::content {

inline constexpr double kBaseboardSize = 720.0;  // metres (observed: local x/y range, 72 cells x 10 m)

struct TrackLink {
    std::int32_t item = -1;  // index of a tkSt or tkBr in the first container
    std::uint8_t flag = 0;   // meaning unknown
};

struct TrackVertex {
    std::int32_t index = 0;  // the item's index; stretches refer to it
    std::uint32_t version = 0;
    std::int16_t boardX = 0;  // the terrain section (baseboard) it lies on
    std::int16_t boardY = 0;
    float x = 0;  // metres inside the baseboard, toward +x
    float y = 0;  // metres inside the baseboard, toward +y
    float z = 0;  // height in metres
    std::vector<TrackLink> links;  // the non-empty slots, in order (1 = line end, 2 = along a line, 3+ = junction?)
    std::uint8_t lastByte = 0;     // the byte after the 4 slots (0, 1 or 2 on the disc; meaning unknown)
    std::vector<std::uint8_t> rest;  // anything after that (empty in every disc file)

    double worldX() const { return boardX * kBaseboardSize + x; }
    double worldY() const { return boardY * kBaseboardSize + y; }
};

struct TrackStretch {
    std::string kind = "tkSt";  // "tkSt", or "tkBr": a stretch wrapped in one more word (meaning not known)
    std::uint32_t wrapperVersion = 0;  // tkBr only: its own first word (2 on the disc)
    std::int32_t index = 0;
    std::uint32_t version = 0;
    std::int32_t from = 0;  // vertex indices
    std::int32_t to = 0;
    Kuid track;             // the track asset
    std::vector<std::uint8_t> rest;  // not decoded, except bit 0x20 of rest[0] (see straight())

    // Bit 0x20 of the first byte after the KUID marks a straight piece (OBSERVED, not traced):
    // in 125 disc sessions, vehicles on pieces with it lie on the straight line between the
    // ends (0-3% more than 0.3 m off); on pieces whose byte is 0 or 16 (most of the rest)
    // 31-57% are off. Values 84, 132, 148 (no 0x20) also had 0% off (about 660 vehicles), but
    // reading 0x40 or 0x80 as straight changes nothing measurable; no other single bit helps.
    bool straight() const { return !rest.empty() && (rest[0] & 0x20) != 0; }
};

struct TrackLayout {
    std::map<std::int32_t, TrackVertex> vertices;
    std::vector<TrackStretch> stretches;
    std::size_t otherItems = 0;  // items that are neither vertices nor tkSt/tkBr
    std::vector<std::string> warnings;
    // Piece index -> position in `stretches` (filled by readTrackLayout; the first wins on a
    // repeat). Code that builds a layout by hand may leave it empty: lookups then scan.
    std::map<std::int32_t, std::size_t> stretchAt;

    const TrackStretch* findStretch(std::int32_t index) const;
};

// Decodes one item, or nullopt if it is not that class or too short.
std::optional<TrackVertex> decodeTrackVertex(const ObjectItem& item);
std::optional<TrackStretch> decodeTrackStretch(const ObjectItem& item);

// Collects every vertex and stretch (tkSt and tkBr) of a parsed mapfile.trk.
// Stretches whose ends are missing, vertex links to unknown pieces, duplicate
// vertex indices and unreadable items become warnings.
TrackLayout readTrackLayout(const RouteObjectFile& trk);

// The port's model of how a stretch curves between its ends (a MODEL, not traced). A
// straight() piece is the straight line between its ends. Otherwise:
// a vertex link's flag says which side of the vertex the piece leaves from (observed: on
// the disc, of 206,782 pairs of pieces at one vertex with different flags, 92% point within
// 30 degrees of opposite ways (69% within 10); of 13,387 same-flag pairs, 83% point within
// 30 degrees of the same way (68% within 10)). Each vertex gets one tangent, from its
// side-0 neighbours toward its side-1 neighbours, except that a piece carries straight on
// from a straight() piece on the vertex's other side; a stretch is a cubic Hermite curve in x/y
// leaving each end along that tangent (on the piece's side), with tangent length
// kCurveTangentScale x the chord. Height is linear along the chord. A vertex with pieces on
// one side only (a line end) uses the chord direction.
// Checked against the 6,071 vehicles of 125 disc sessions (they sit on their track): within
// 0.5 m of the model 97.0%, of the straight chords 87.0%; within 3 m 99.5% vs 95.6%. For
// the 964 vehicles more than 0.3 m from every chord: median 0.04 m from the model, 90% within 1.1 m.
inline constexpr double kCurveTangentScale = 1.0;

struct TrackCurve {
    double p0[3] = {0, 0, 0}, p1[3] = {0, 0, 0};  // world metres (x, y, z)
    double t0[2] = {0, 0}, t1[2] = {0, 0};        // x/y tangents at p0 and p1 (both pointing from p0 toward p1)
    // The point at s in 0..1 (0 = from, 1 = to).
    void point(double s, double out[3]) const;
    // Largest distance between the curve and its chord, sampled (metres).
    double bulge() const;
};

// nullopt if an end is missing or both ends are (almost) at the same place.
std::optional<TrackCurve> trackCurve(const TrackLayout& layout, const TrackStretch& stretch,
                                     double tangentScale = kCurveTangentScale);

// The nearest point on the modelled track (trackCurve) to a world position.
struct TrackSample {
    double x = 0, y = 0, z = 0;  // world metres; z interpolated along the piece
    double heading = 0;          // radians, anticlockwise from +x, in the piece's from -> to direction
    double distance = 0;         // from the asked position, in x/y
    std::int32_t piece = -1;     // the stretch's index
};

// Finds the nearest track point within a search distance. Built for the pieces that come
// within `radius` of (centreX, centreY) (whole route: a huge radius), cut into pieces of
// about `step` metres. Read-only on the layout, which must outlive it.
class TrackLocator {
public:
    TrackLocator(const TrackLayout& layout, double centreX, double centreY, double radius, double step = 2.0);
    // nullopt if no track lies within maxDistance (at most kCell metres).
    std::optional<TrackSample> nearest(double x, double y, double maxDistance = 5.0) const;
    std::size_t segments() const { return segs_.size(); }

    static constexpr double kCell = 25.0;  // grid cell, metres

private:
    struct Seg {
        double a[3], b[3];
        std::int32_t piece;
    };
    std::vector<Seg> segs_;
    std::map<std::pair<std::int64_t, std::int64_t>, std::vector<std::size_t>> grid_;
};

}  // namespace trainz::content
