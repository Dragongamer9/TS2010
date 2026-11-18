#include "PngWriter.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace trainz::common {

namespace {

std::array<std::uint32_t, 256> makeCrcTable() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        t[n] = c;
    }
    return t;
}

void putBE32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

void chunk(std::vector<std::uint8_t>& out, const char type[4], const std::vector<std::uint8_t>& data) {
    putBE32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    putBE32(out, crc32(std::span<const std::uint8_t>(out.data() + start, out.size() - start)));
}

}  // namespace

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc) {
    static const auto table = makeCrcTable();
    crc = ~crc;
    for (std::uint8_t b : data) {
        crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

std::vector<std::uint8_t> encodePngRgba(std::uint32_t width, std::uint32_t height,
                                        std::span<const std::uint8_t> rgba) {
    if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
        rgba.size() != static_cast<std::size_t>(width) * height * 4) {
        throw std::invalid_argument("encodePngRgba: size does not match the pixel data");
    }
    // Raw scanlines: filter byte 0 + RGBA row.
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    std::vector<std::uint8_t> raw;
    raw.reserve((rowBytes + 1) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(y * rowBytes),
                   rgba.begin() + static_cast<std::ptrdiff_t>((y + 1) * rowBytes));
    }
    // zlib stream with stored deflate blocks.
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::uint8_t v : raw) {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    std::size_t pos = 0;
    do {
        const std::size_t len = std::min<std::size_t>(65535, raw.size() - pos);
        const bool last = pos + len == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(len));
        z.push_back(static_cast<std::uint8_t>(len >> 8));
        z.push_back(static_cast<std::uint8_t>(~len));
        z.push_back(static_cast<std::uint8_t>(~len >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
    } while (pos < raw.size());
    putBE32(z, (b << 16) | a);

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    putBE32(ihdr, width);
    putBE32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit, RGBA, deflate, no filter variants, no interlace
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

}  // namespace trainz::common
