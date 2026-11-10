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

struct MeshFile {
    std::vector<std::uint32_t> infoWords;  // INFO; version 102 (44 bytes) or 104 (68 bytes)
    std::vector<MeshPart> parts;
    std::vector<std::string> otherChunks;  // top level: INFL, ATCH, SKEL, PMIN (kept unparsed)
    std::vector<std::string> warnings;
};

// Throws ContentError on damage. Data must stay alive only during the call.
MeshFile parseMesh(std::span<const std::uint8_t> data);

// Parsers for the two chunk types, exposed for tests.
Material parseMaterialChunk(std::span<const std::uint8_t> data);
MeshGeometry parseGeometryChunk(std::span<const std::uint8_t> data);

}  // namespace trainz::content
