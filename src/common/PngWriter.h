// Minimal PNG encoder for viewing decoded content (no external libraries).
//
// Writes 8-bit RGBA, non-interlaced, with "stored" (uncompressed) deflate
// blocks, so files are about as large as the raw pixels. Good enough for
// checking textures by eye; not meant for shipping assets.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace trainz::common {

std::vector<std::uint8_t> encodePngRgba(std::uint32_t width, std::uint32_t height,
                                        std::span<const std::uint8_t> rgba);

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc = 0);

}  // namespace trainz::common
