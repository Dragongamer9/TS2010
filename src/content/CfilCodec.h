// CFIL: the compression wrapper the game uses for files stored in .ja archives.
//
// Based on KernelStandard_7c.dll (TS2010 SP2 build), see docs/formats/ja.md:
//   - header check and size read:   0x10020053 .. 0x10020097
//   - block walk (u16 headers):     0x10020dfd .. 0x10020e2f
//   - BPE expander:                 0x1000d130
//   - compressor (not ported):      0x1000ccc0, writer 0x10020237
//
// Layout (archive version 0x102):
//   "CFIL"  u32 version  u32 original_size
//   blocks: u16 header; if header & 0x8000 -> (header & 0x7FFF) raw bytes,
//           else -> header bytes of BPE data that expand to <= 16 KiB.
// In version 0x101 archives the reader only consumes the 4-byte "CFIL" magic,
// and the original size comes from the archive entry instead.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace trainz::content {

inline constexpr std::size_t kCfilBlockSize = 0x4000;  // 16 KiB, from 0x100202a8 / 0x10020e12
inline constexpr std::uint16_t kCfilRawFlag = 0x8000;

struct CfilHeader {
    std::size_t headerSize = 0;       // 4 or 12 bytes
    std::uint32_t version = 0;        // 0 when the header has no version field
    std::uint32_t originalSize = 0;   // decoded size
    bool sizeFromHeader = false;      // false: size was taken from the archive entry
};

// True if the data starts with the 4-byte "CFIL" magic.
bool hasCfilMagic(std::span<const std::uint8_t> data);

// Parses the CFIL header. archiveVersion is the ARC0 version of the archive the
// data came from (0x101 or 0x102); entryOriginalSize is used when the header has
// no size of its own. Throws ContentError on a bad header.
CfilHeader parseCfilHeader(std::span<const std::uint8_t> data, std::uint32_t archiveVersion,
                           std::uint32_t entryOriginalSize);

// Expands one BPE-compressed block payload, appending to out. Never lets out
// grow past maxOutputSize. Throws ContentError on damaged data.
void expandBpeBlock(std::span<const std::uint8_t> payload, std::vector<std::uint8_t>& out,
                    std::size_t maxOutputSize);

// Decodes a whole CFIL file held in memory. Throws ContentError on damaged data.
std::vector<std::uint8_t> decodeCfil(std::span<const std::uint8_t> data, std::uint32_t archiveVersion,
                                     std::uint32_t entryOriginalSize);

}  // namespace trainz::content
