// Little-endian loads. The game was built for 32-bit x86, so every integer in
// its files is little-endian. These helpers read byte by byte so they work the
// same on any host. Callers check the bounds before calling them.
#pragma once

#include <cstdint>

namespace trainz::content {

inline std::uint16_t loadU16LE(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

inline std::uint32_t loadU32LE(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace trainz::content
