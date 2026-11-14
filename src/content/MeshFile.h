// Jet indexed meshes: .im and .pm (JIRF form "IDXM"). See docs/formats/mesh.md.
//
// Layout worked out from the data and checked on every .im/.pm in Base.ja
// and Builtin\8.ja (7,982 parts): every GEOM and MATL chunk is accounted for
// byte for byte, every index is below its vertex count, and the normals are
// unit length. The reader code in Render3DFoundation_7c.dll
// (IndexedMeshDefinition / IndexedMeshDefinitionOld) has NOT been traced.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace trainz::content {

struct MaterialTexture {
    std::uint32_t type = 0;     // 1 colour map; 9 reflection; 8 normal map; 5 seen (meaning unknown)
    std::string originalPath;   // the artist's Windows path, e.g. "C:\\...\\wall.texture"
    std::string fileName;       // last part of originalPath: the .texture in the asset folder
    float amount = 0;           // e.g. 1.0, 0.3
};

struct Material {
    std::uint32_t version = 0;  // 102 or 103 seen
    std::string name;           // e.g. "*shed*6A392CC2*floor*m.onetex" or "wall.m.tbumptex"
    std::string kind;           // the "m.xxx" part: m.onetex, m.notex, m.reflect, m.gloss, m.tbumptex ...
    std::array<float, 16> values{};  // colours and shininess; individual meaning not confirmed
    std::vector<MaterialTexture> textures;
};

struct MeshVertex {
    float x = 0, y = 0, z = 0;  // Z is up (checked by rendering real meshes)
    float u = 0, v = 0;
};

struct MeshGeometry {
    std::uint32_t version = 0;  // 101, 102, 200 or 201
    std::uint32_t flags = 0;    // v201 word 2; 1 = an extra 3-float value per vertex follows the face normals
    float headerValue = 0;      // the float in the header; meaning unknown
    std::string boneName;       // v200/201 optional name block (e.g. "b.r.door1"); empty if none
    std::vector<MeshVertex> vertices;
    std::vector<std::uint16_t> indices;  // triangles: 3 per face
    std::vector<std::array<float, 3>> vertexNormals;
    std::vector<std::array<float, 3>> faceNormals;
    std::vector<std::array<float, 3>> extraPerVertex;  // only when flags & 1; meaning unknown
};

struct MeshPart {
    std::uint32_t index = 0;  // from the CHNK header
    Material material;
    MeshGeometry geometry;
    std::vector<std::string> otherChunks;  // e.g. "NINF" in .pm files (kept unparsed)
};

// A named attachment point ("a.bog0", "a.limfront", ...) from the ATCH chunk.
// Layout (observed, all 5,868 ATCH chunks on the disc decode exactly to their end):
//   u32 100, u32 count, then per point: u32 nameBytes (bit 30 set: the name is UTF-16LE),
//   name[nameBytes] (NUL-padded), 12 floats = a 3x3 rotation then a position.
// Every rotation on the disc has unit-length rows. Which index order the game uses is not
// traced; the port assumes Direct3D's: rotation[0..2] is where the point's own x axis
// points, [3..5] its y, [6..8] its z.
struct MeshAttachment {
    std::string name;  // as stored; UTF-16 names (bit 30) are returned as UTF-8
    float rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float position[3] = {0, 0, 0};  // metres, in the mesh's own space
};

struct MeshFile {
    std::vector<std::uint32_t> infoWords;  // INFO; version 102 (44 bytes) or 104 (68 bytes)
    std::vector<MeshPart> parts;
    std::vector<MeshAttachment> attachments;  // from ATCH; empty if none or unreadable (see warnings)
    std::vector<std::string> otherChunks;  // top level: INFL, SKEL, PMIN (kept unparsed)
    std::vector<std::string> warnings;
};

// Throws ContentError on damage. Data must stay alive only during the call.
MeshFile parseMesh(std::span<const std::uint8_t> data);

// For tools: parseMesh, and if that fails, retry once on the bytes repaired
// by repairTextModeDamage (see JirfFile.h). *repaired tells which happened.
MeshFile parseMeshOrRepair(std::span<const std::uint8_t> data, bool* repaired = nullptr);

// Parsers for the two chunk types, exposed for tests.
Material parseMaterialChunk(std::span<const std::uint8_t> data);
MeshGeometry parseGeometryChunk(std::span<const std::uint8_t> data);
// Throws ContentError if the chunk does not follow the layout above or does not end exactly.
std::vector<MeshAttachment> parseAttachmentChunk(std::span<const std::uint8_t> data);

}  // namespace trainz::content
