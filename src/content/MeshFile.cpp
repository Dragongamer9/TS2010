#include "MeshFile.h"

#include "ByteOrder.h"
#include "ContentError.h"
#include "JirfFile.h"

#include <cstring>

namespace trainz::content {

namespace {

constexpr std::uint32_t kMaxVertices = 65536;  // indices are 16-bit
constexpr std::uint32_t kMaxFaces = 4u << 20;
constexpr std::uint32_t kMaxNameBlock = 4096;

class Reader {
public:
    Reader(std::span<const std::uint8_t> d, const char* what) : d_(d), what_(what) {}

    void need(std::size_t n) const {
        if (d_.size() - pos_ < n) {
            throw ContentError(std::string(what_) + ": data ends early at byte " + std::to_string(pos_) + " (need " +
                               std::to_string(n) + " more)");
        }
    }
    std::uint32_t u32() {
        need(4);
        const std::uint32_t v = loadU32LE(d_.data() + pos_);
        pos_ += 4;
        return v;
    }
    std::uint16_t u16() {
        need(2);
        const std::uint16_t v = loadU16LE(d_.data() + pos_);
        pos_ += 2;
        return v;
    }
    float f32() {
        const std::uint32_t bits = u32();
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }
    std::array<float, 3> vec3() { return {f32(), f32(), f32()}; }
    // A fixed-size text field; the text ends at the first NUL.
    std::string text(std::size_t n) {
        need(n);
        const auto* p = reinterpret_cast<const char*>(d_.data() + pos_);
        const void* nul = std::memchr(p, 0, n);
        const std::size_t len = nul ? static_cast<std::size_t>(static_cast<const char*>(nul) - p) : n;
        pos_ += n;
        return std::string(p, len);
    }
    // A length-prefixed MATL text field. Bit 30 of the length marks UTF-16LE
    // text (seen in 56 materials in Builtin\1.ja-4.ja, e.g. coalman.im); the
    // low 30 bits are the byte count. Returned as UTF-8.
    std::string matlText(std::uint32_t lengthField) {
        constexpr std::uint32_t kWide = 0x40000000u;
        const std::uint32_t n = lengthField & ~kWide;
        if (n > kMaxNameBlock) {
            throw ContentError(std::string(what_) + ": text of " + std::to_string(n) + " bytes is too long");
        }
        if (!(lengthField & kWide)) {
            return text(n);
        }
        need(n);
        std::string out;
        const std::uint8_t* p = d_.data() + pos_;
        for (std::size_t i = 0; i + 1 < n; i += 2) {
            std::uint32_t c = static_cast<std::uint32_t>(p[i] | (p[i + 1] << 8));
            if (c == 0) {
                break;
            }
            if (c >= 0xD800 && c <= 0xDFFF) {
                c = '?';  // surrogates are not expected in these names
            }
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
            } else if (c < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xE0 | (c >> 12)));
                out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
        pos_ += n;
        return out;
    }
    std::size_t pos() const { return pos_; }
    std::size_t left() const { return d_.size() - pos_; }

private:
    std::span<const std::uint8_t> d_;
    const char* what_;
    std::size_t pos_ = 0;
};

std::string materialKind(const std::string& name) {
    // The last "m.<kind>" that starts the name or follows '*' or '.'
    for (std::size_t p = name.size(); p-- > 0;) {
        if (name.compare(p, 2, "m.") == 0 && (p == 0 || name[p - 1] == '*' || name[p - 1] == '.')) {
            return name.substr(p);
        }
    }
    return {};
}

std::string lastPathPart(const std::string& path) {
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

Material parseMaterialChunk(std::span<const std::uint8_t> data) {
    Reader r(data, "MATL");
    Material m;
    m.version = r.u32();
    if (m.version != 102 && m.version != 103) {
        throw ContentError("MATL version " + std::to_string(m.version) + " is unknown (102 and 103 are known)");
    }
    m.name = r.matlText(r.u32());
    m.kind = materialKind(m.name);
    for (auto& v : m.values) {
        v = r.f32();
    }
    const std::uint32_t count = r.u32();
    if (count > 32) {
        throw ContentError("MATL lists " + std::to_string(count) + " textures");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        MaterialTexture t;
        t.type = r.u32();
        t.originalPath = r.matlText(r.u32());
        t.fileName = lastPathPart(t.originalPath);
        t.amount = r.f32();
        m.textures.push_back(std::move(t));
    }
    if (r.left() != 0) {
        throw ContentError("MATL has " + std::to_string(r.left()) + " unexplained bytes at the end");
    }
    return m;
}

std::vector<MeshAttachment> parseAttachmentChunk(std::span<const std::uint8_t> data) {
    Reader r(data, "ATCH");
    const std::uint32_t version = r.u32();
    if (version != 100) {
        throw ContentError("ATCH version " + std::to_string(version) + " (only 100 is known)");
    }
    const std::uint32_t count = r.u32();
    if (count > r.left() / 52) {  // each point is at least 4 + 48 bytes
        throw ContentError("ATCH claims " + std::to_string(count) + " points, more than its size allows");
    }
    std::vector<MeshAttachment> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t lengthField = r.u32();
        if ((lengthField & 0x80000000u) != 0) {
            throw ContentError("ATCH point " + std::to_string(i) + ": unknown flag in its name length");
        }
        MeshAttachment a;
        a.name = r.matlText(lengthField);
        for (float& f : a.rotation) {
            f = r.f32();
        }
        for (float& f : a.position) {
            f = r.f32();
        }
        out.push_back(std::move(a));
    }
    if (r.left() != 0) {
        throw ContentError("ATCH has " + std::to_string(r.left()) + " bytes after its last point");
    }
    return out;
}

MeshGeometry parseGeometryChunk(std::span<const std::uint8_t> data) {
    Reader r(data, "GEOM");
    MeshGeometry g;
    g.version = r.u32();
    if (g.version != 101 && g.version != 102 && g.version != 200 && g.version != 201) {
        throw ContentError("GEOM version " + std::to_string(g.version) + " is unknown (101, 102, 200, 201 are known)");
    }
    (void)r.u32();  // word 1: always 4 in the data
    if (g.version == 201) {
        g.flags = r.u32();
        if (g.flags > 1) {
            throw ContentError("GEOM flags value " + std::to_string(g.flags) + " is unknown");
        }
    }
    g.headerValue = r.f32();
    const std::uint32_t nv = r.u32();
    const std::uint32_t nt = r.u32();
    const std::uint32_t ni = r.u32();
    const std::uint32_t nf = r.u32();
    if (g.version != 101) {
        (void)r.u32();  // always 0 in the data; v101 does not have this word
    }
    std::uint32_t nameBlock = 0;
    if (g.version >= 200) {
        nameBlock = r.u32();
    }
    if (nv > kMaxVertices || nt > kMaxFaces || nf != nt || static_cast<std::uint64_t>(ni) != 3ull * nt ||
        nameBlock > kMaxNameBlock) {
        throw ContentError("GEOM counts do not fit together (" + std::to_string(nv) + " vertices, " +
                           std::to_string(nt) + " triangles, " + std::to_string(ni) + " indices, " +
                           std::to_string(nf) + " face normals)");
    }
    const bool extra = (g.flags & 1u) != 0;
    const std::uint64_t want = static_cast<std::uint64_t>(nameBlock) + 20ull * nv + 2ull * ni + 12ull * nv +
                               12ull * nf + (extra ? 12ull * nv : 0);
    if (want != r.left()) {
        throw ContentError("GEOM is " + std::to_string(data.size()) + " bytes but its counts need " +
                           std::to_string(want + r.pos()));
    }
    g.boneName = r.text(nameBlock);
    g.vertices.resize(nv);
    for (auto& v : g.vertices) {
        v.x = r.f32();
        v.y = r.f32();
        v.z = r.f32();
        v.u = r.f32();
        v.v = r.f32();
    }
    g.indices.resize(ni);
    for (auto& i : g.indices) {
        i = r.u16();
        if (i >= nv) {
            throw ContentError("GEOM index " + std::to_string(i) + " is not below the vertex count " +
                               std::to_string(nv));
        }
    }
    g.vertexNormals.resize(nv);
    for (auto& n : g.vertexNormals) {
        n = r.vec3();
    }
    g.faceNormals.resize(nf);
    for (auto& n : g.faceNormals) {
        n = r.vec3();
    }
    if (extra) {
        g.extraPerVertex.resize(nv);
        for (auto& e : g.extraPerVertex) {
            e = r.vec3();
        }
    }
    return g;
}

MeshFile parseMesh(std::span<const std::uint8_t> data) {
    const JirfFile jirf = parseJirf(data);
    if (jirf.form != "IDXM") {
        throw ContentError("not a mesh (JIRF form is '" + jirf.form + "', expected 'IDXM')");
    }
    MeshFile mesh;
    mesh.warnings = jirf.warnings;
    for (const auto& c : jirf.chunks) {
        if (c.tag == "INFO") {
            for (std::size_t i = 0; i + 4 <= c.data.size(); i += 4) {
                mesh.infoWords.push_back(loadU32LE(c.data.data() + i));
            }
            continue;
        }
        if (c.tag == "ATCH") {
            try {
                mesh.attachments = parseAttachmentChunk(c.data);
            } catch (const ContentError& e) {
                mesh.warnings.push_back(std::string("ATCH not read: ") + e.what());  // the geometry still loads
            }
            continue;
        }
        if (c.tag != "CHNK") {
            mesh.otherChunks.push_back(c.tag);
            continue;
        }
        if (c.data.size() < 8) {
            throw ContentError("CHNK at byte " + std::to_string(c.offset) + " is too small");
        }
        if (mesh.parts.size() >= 4096) {
            throw ContentError("mesh has more than 4096 parts");
        }
        MeshPart part;
        if (loadU32LE(c.data.data()) != 100) {
            mesh.warnings.push_back("CHNK header value " + std::to_string(loadU32LE(c.data.data())) +
                                    " (100 everywhere in the data)");
        }
        part.index = loadU32LE(c.data.data() + 4);
        bool haveMat = false;
        bool haveGeom = false;
        for (const auto& s : parseJirfChunks(c.data.subspan(8), c.offset + 8)) {
            if (s.tag == "MATL" && !haveMat) {
                part.material = parseMaterialChunk(s.data);
                haveMat = true;
            } else if (s.tag == "GEOM" && !haveGeom) {
                part.geometry = parseGeometryChunk(s.data);
                haveGeom = true;
            } else {
                part.otherChunks.push_back(s.tag);
            }
        }
        if (!haveGeom) {
            throw ContentError("CHNK " + std::to_string(part.index) + " has no GEOM");
        }
        if (!haveMat) {
            mesh.warnings.push_back("CHNK " + std::to_string(part.index) + " has no MATL");
        }
        mesh.parts.push_back(std::move(part));
    }
    return mesh;
}

MeshFile parseMeshOrRepair(std::span<const std::uint8_t> data, bool* repaired) {
    if (repaired) {
        *repaired = false;
    }
    try {
        return parseMesh(data);
    } catch (const ContentError&) {
        const auto fixed = repairTextModeDamage(data);
        if (!fixed) {
            throw;
        }
        MeshFile m = parseMesh(*fixed);
        m.warnings.push_back("file looks damaged by a text-mode transfer (CR LF for LF); repaired in memory to a consistent file");
        if (repaired) {
            *repaired = true;
        }
        return m;
    }
}

}  // namespace trainz::content
