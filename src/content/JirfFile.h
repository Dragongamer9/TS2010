// JIRF: the chunk container Jet uses for .texture (form "MMTF"), .im / .pm
// (form "IDXM") and .kin (form "ANIM").
//
//   "JIRF"  u32 size (bytes after this field)  char form[4]
//   then chunks: char tag[4]  u32 size  u8 data[size]   (no padding between chunks)
//
// The reader in RenderSystem_7c.dll is Jet::JIRFFile (used by
// TextureResource::Load, 0x10045500). Layout checked on every .texture in
// Base.ja, Data.ja and Builtin\8.ja. See docs/formats/texture.md.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trainz::content {

struct JirfChunk {
    std::string tag;                   // 4 characters, e.g. "INFO", "MMAP", "CHNK"
    std::size_t offset = 0;            // offset of the chunk data in the file
    std::span<const std::uint8_t> data;  // points into the parsed buffer
};

struct JirfFile {
    std::string form;  // e.g. "MMTF"
    std::vector<JirfChunk> chunks;
    std::vector<std::string> warnings;

    const JirfChunk* first(std::string_view tag) const;
};

// Parses the top level. The returned spans point into `data`, which must stay
// alive. Throws ContentError on damage.
JirfFile parseJirf(std::span<const std::uint8_t> data);

// Parses a run of chunks (tag, u32 size, data) inside a chunk's data, e.g. the
// sub-chunks of a mesh "CHNK". baseOffset is added to the reported offsets.
std::vector<JirfChunk> parseJirfChunks(std::span<const std::uint8_t> data, std::size_t baseOffset = 0);

}  // namespace trainz::content
