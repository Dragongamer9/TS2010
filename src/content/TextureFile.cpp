#include "TextureFile.h"

#include "ByteOrder.h"
#include "ContentError.h"
#include "JirfFile.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace trainz::content {

namespace {

constexpr std::size_t kInfoWordFormat = 4;
constexpr std::size_t kInfoWordWidth = 5;
constexpr std::size_t kInfoWordHeight = 6;

struct Rgb {
    std::uint8_t r, g, b;
};

Rgb rgb565(std::uint16_t v) {
    const unsigned r = (v >> 11) & 31u;
    const unsigned g = (v >> 5) & 63u;
    const unsigned b = v & 31u;
    return {static_cast<std::uint8_t>((r * 255 + 15) / 31), static_cast<std::uint8_t>((g * 255 + 31) / 63),
            static_cast<std::uint8_t>((b * 255 + 15) / 31)};
}

std::uint8_t mix(unsigned a, unsigned b, unsigned wa, unsigned wb) {
    return static_cast<std::uint8_t>((a * wa + b * wb) / (wa + wb));
}

// Standard S3TC colour block (8 bytes) into a 4x4 RGBA tile. For DXT1 a
// "c0 <= c1" block has 3 colours plus transparent black.
void decodeColourBlock(const std::uint8_t* blk, bool dxt1, std::array<std::uint8_t, 64>& tile) {
    const std::uint16_t c0 = loadU16LE(blk);
    const std::uint16_t c1 = loadU16LE(blk + 2);
    const std::uint32_t idx = loadU32LE(blk + 4);
    const Rgb a = rgb565(c0);
    const Rgb b = rgb565(c1);
    std::array<std::array<std::uint8_t, 4>, 4> pal{};
    pal[0] = {a.r, a.g, a.b, 255};
    pal[1] = {b.r, b.g, b.b, 255};
    if (!dxt1 || c0 > c1) {
        pal[2] = {mix(a.r, b.r, 2, 1), mix(a.g, b.g, 2, 1), mix(a.b, b.b, 2, 1), 255};
        pal[3] = {mix(a.r, b.r, 1, 2), mix(a.g, b.g, 1, 2), mix(a.b, b.b, 1, 2), 255};
    } else {
        pal[2] = {mix(a.r, b.r, 1, 1), mix(a.g, b.g, 1, 1), mix(a.b, b.b, 1, 1), 255};
        pal[3] = {0, 0, 0, 0};
    }
    for (unsigned i = 0; i < 16; ++i) {
        const auto& c = pal[(idx >> (2 * i)) & 3u];
        std::copy(c.begin(), c.end(), tile.begin() + 4 * i);
    }
}

void decodeDxt(const MipLevel& mip, PixelFormat f, std::vector<std::uint8_t>& out) {
    const std::size_t blockBytes = f == PixelFormat::DXT1 ? 8 : 16;
    const std::uint32_t bw = std::max(1u, (mip.width + 3) / 4);
    const std::uint32_t bh = std::max(1u, (mip.height + 3) / 4);
    std::array<std::uint8_t, 64> tile{};
    const std::uint8_t* p = mip.data.data();
    for (std::uint32_t by = 0; by < bh; ++by) {
        for (std::uint32_t bx = 0; bx < bw; ++bx, p += blockBytes) {
            if (f == PixelFormat::DXT1) {
                decodeColourBlock(p, true, tile);
            } else {
                decodeColourBlock(p + 8, false, tile);
                if (f == PixelFormat::DXT3) {
                    for (unsigned i = 0; i < 16; ++i) {
                        const unsigned nib = (p[i / 2] >> (4 * (i % 2))) & 15u;
                        tile[4 * i + 3] = static_cast<std::uint8_t>(nib * 17);
                    }
                } else {  // DXT5: two endpoints and 3-bit indices
                    const unsigned a0 = p[0];
                    const unsigned a1 = p[1];
                    std::array<std::uint8_t, 8> ap{};
                    ap[0] = static_cast<std::uint8_t>(a0);
                    ap[1] = static_cast<std::uint8_t>(a1);
                    if (a0 > a1) {
                        for (unsigned i = 1; i < 7; ++i) {
                            ap[i + 1] = static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
                        }
                    } else {
                        for (unsigned i = 1; i < 5; ++i) {
                            ap[i + 1] = static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
                        }
                        ap[6] = 0;
                        ap[7] = 255;
                    }
                    std::uint64_t bits = 0;
                    for (int i = 0; i < 6; ++i) {
                        bits |= static_cast<std::uint64_t>(p[2 + i]) << (8 * i);
                    }
                    for (unsigned i = 0; i < 16; ++i) {
                        tile[4 * i + 3] = ap[(bits >> (3 * i)) & 7u];
                    }
                }
            }
            for (unsigned i = 0; i < 16; ++i) {
                const std::uint32_t x = bx * 4 + i % 4;
                const std::uint32_t y = by * 4 + i / 4;
                if (x < mip.width && y < mip.height) {
                    const std::size_t q = (static_cast<std::size_t>(y) * mip.width + x) * 4;
                    std::copy(tile.begin() + 4 * i, tile.begin() + 4 * i + 4, out.begin() + static_cast<std::ptrdiff_t>(q));
                }
            }
        }
    }
}

std::uint8_t expand(unsigned v, unsigned bits) {
    const unsigned max = (1u << bits) - 1;
    return static_cast<std::uint8_t>((v * 255 + max / 2) / max);
}

}  // namespace

const char* pixelFormatName(PixelFormat f) {
    switch (f) {
        case PixelFormat::Null: return "NULL";
        case PixelFormat::ARGB8888: return "ARGB8888";
        case PixelFormat::ABGR8888: return "ABGR8888";
        case PixelFormat::RGB0888: return "RGB0888";
        case PixelFormat::BGR0888: return "BGR0888";
        case PixelFormat::RGB888: return "RGB888";
        case PixelFormat::BGR888: return "BGR888";
        case PixelFormat::ARGB4444: return "ARGB4444";
        case PixelFormat::RGB565: return "RGB565";
        case PixelFormat::RGB555: return "RGB555";
        case PixelFormat::DXT1: return "DXT1";
        case PixelFormat::DXT3: return "DXT3";
        case PixelFormat::DXT5: return "DXT5";
    }
    return "unknown";
}

std::optional<std::size_t> imageByteSize(PixelFormat f, std::uint32_t w, std::uint32_t h) {
    // Bounded first: at most 8192 x 8192 x 4 = 256 MiB, far from any overflow.
    if (w == 0 || h == 0 || w > kMaxTextureSize || h > kMaxTextureSize) {
        return std::nullopt;
    }
    const auto pixels = static_cast<std::size_t>(w) * h;
    const std::size_t blocks = static_cast<std::size_t>(std::max(1u, (w + 3) / 4)) * std::max(1u, (h + 3) / 4);
    switch (f) {
        case PixelFormat::ARGB8888:
        case PixelFormat::ABGR8888:
        case PixelFormat::RGB0888:
        case PixelFormat::BGR0888:
            return pixels * 4;
        case PixelFormat::RGB888:
        case PixelFormat::BGR888:
            return pixels * 3;
        case PixelFormat::ARGB4444:
        case PixelFormat::RGB565:
        case PixelFormat::RGB555:
            return pixels * 2;
        case PixelFormat::DXT1:
            return blocks * 8;
        case PixelFormat::DXT3:
        case PixelFormat::DXT5:
            return blocks * 16;
        case PixelFormat::Null:
            break;
    }
    return std::nullopt;
}

TextureFile parseTexture(std::span<const std::uint8_t> data) {
    // One file on the disc (Builtin\6.ja parkeerplaats.texture.texture) is a
    // text descriptor ("Primary=parkeerplaats.bmp") under a .texture name.
    static constexpr char kDescriptor[] = "Primary=";
    if (data.size() >= sizeof kDescriptor - 1 && std::memcmp(data.data(), kDescriptor, sizeof kDescriptor - 1) == 0) {
        throw ContentError("this is a texture descriptor text file (.texture.txt style), not a binary .texture");
    }
    const JirfFile jirf = parseJirf(data);
    if (jirf.form != "MMTF") {
        throw ContentError("not a texture (JIRF form is '" + jirf.form + "', expected 'MMTF')");
    }
    TextureFile tex;
    tex.warnings = jirf.warnings;
    const JirfChunk* info = jirf.first("INFO");
    if (info == nullptr) {
        throw ContentError("texture has no INFO chunk");
    }
    if (info->data.size() < 28 || info->data.size() % 4 != 0) {
        throw ContentError("texture INFO chunk is " + std::to_string(info->data.size()) + " bytes");
    }
    for (std::size_t i = 0; i < info->data.size(); i += 4) {
        tex.info.words.push_back(loadU32LE(info->data.data() + i));
    }
    tex.info.version = tex.info.words[0];
    tex.info.mipCount = tex.info.words[2];
    tex.info.format = static_cast<PixelFormat>(tex.info.words[kInfoWordFormat]);
    tex.info.width = tex.info.words[kInfoWordWidth];
    tex.info.height = tex.info.words[kInfoWordHeight];
    if (tex.info.version != 0x107 && tex.info.version != 0x106 && tex.info.version != 0x103) {
        tex.warnings.push_back("INFO version " + std::to_string(tex.info.version) +
                               " has not been seen before (259, 262 and 263 have)");
    }
    const auto& t = tex.info;
    if (t.width == 0 || t.height == 0 || t.width > kMaxTextureSize || t.height > kMaxTextureSize) {
        throw ContentError("texture size " + std::to_string(t.width) + "x" + std::to_string(t.height) +
                           " is not allowed");
    }
    if (!imageByteSize(t.format, t.width, t.height)) {
        throw ContentError("texture pixel format " + std::to_string(static_cast<std::uint32_t>(t.format)) +
                           " is unknown");
    }

    std::vector<const JirfChunk*> mmaps;
    for (const auto& c : jirf.chunks) {
        if (c.tag == "MMAP") {
            mmaps.push_back(&c);
        } else if (c.tag != "INFO") {
            tex.warnings.push_back("unexpected chunk '" + c.tag + "' ignored");
        }
    }
    if (mmaps.empty() || mmaps.size() != t.mipCount || mmaps.size() > 14) {
        throw ContentError("INFO says " + std::to_string(t.mipCount) + " mip levels but the file has " +
                           std::to_string(mmaps.size()));
    }
    // Stored smallest first: the last MMAP is level 0 (full size).
    const std::size_t n = mmaps.size();
    tex.mips.resize(n);
    for (std::size_t level = 0; level < n; ++level) {
        const JirfChunk& c = *mmaps[n - 1 - level];
        MipLevel& m = tex.mips[level];
        m.width = std::max(1u, t.width >> level);
        m.height = std::max(1u, t.height >> level);
        const std::size_t want = *imageByteSize(t.format, m.width, m.height);
        if (c.data.size() != want) {
            throw ContentError("mip level " + std::to_string(level) + " (" + std::to_string(m.width) + "x" +
                               std::to_string(m.height) + " " + pixelFormatName(t.format) + ") is " +
                               std::to_string(c.data.size()) + " bytes, expected " + std::to_string(want));
        }
        m.data.assign(c.data.begin(), c.data.end());
    }
    return tex;
}

std::vector<std::uint8_t> decodeToRgba8(const MipLevel& mip, PixelFormat f) {
    if (mip.width == 0 || mip.height == 0 || mip.width > kMaxTextureSize || mip.height > kMaxTextureSize) {
        throw ContentError("mip size " + std::to_string(mip.width) + "x" + std::to_string(mip.height) +
                           " is not allowed (1.." + std::to_string(kMaxTextureSize) + ")");
    }
    const auto want = imageByteSize(f, mip.width, mip.height);
    if (!want || mip.data.size() != *want) {
        throw ContentError("mip data does not match its size and format");
    }
    const std::size_t pixels = static_cast<std::size_t>(mip.width) * mip.height;
    std::vector<std::uint8_t> out(pixels * 4);
    const std::uint8_t* s = mip.data.data();
    switch (f) {
        case PixelFormat::DXT1:
        case PixelFormat::DXT3:
        case PixelFormat::DXT5:
            decodeDxt(mip, f, out);
            return out;
        default:
            break;
    }
    for (std::size_t i = 0; i < pixels; ++i) {
        std::uint8_t* d = out.data() + 4 * i;
        switch (f) {
            case PixelFormat::ARGB8888:  // little-endian ARGB dword = bytes B, G, R, A (checked visually)
                d[0] = s[4 * i + 2], d[1] = s[4 * i + 1], d[2] = s[4 * i], d[3] = s[4 * i + 3];
                break;
            case PixelFormat::RGB0888:  // same order, 4th byte unused (opaque)
                d[0] = s[4 * i + 2], d[1] = s[4 * i + 1], d[2] = s[4 * i], d[3] = 255;
                break;
            case PixelFormat::ABGR8888:  // by name only
                d[0] = s[4 * i], d[1] = s[4 * i + 1], d[2] = s[4 * i + 2], d[3] = s[4 * i + 3];
                break;
            case PixelFormat::BGR0888:  // by name only
                d[0] = s[4 * i], d[1] = s[4 * i + 1], d[2] = s[4 * i + 2], d[3] = 255;
                break;
            case PixelFormat::RGB888:  // by name only
                d[0] = s[3 * i + 2], d[1] = s[3 * i + 1], d[2] = s[3 * i], d[3] = 255;
                break;
            case PixelFormat::BGR888:  // by name only
                d[0] = s[3 * i], d[1] = s[3 * i + 1], d[2] = s[3 * i + 2], d[3] = 255;
                break;
            case PixelFormat::ARGB4444: {  // by name only
                const unsigned v = loadU16LE(s + 2 * i);
                d[0] = expand((v >> 8) & 15u, 4), d[1] = expand((v >> 4) & 15u, 4), d[2] = expand(v & 15u, 4),
                d[3] = expand((v >> 12) & 15u, 4);
                break;
            }
            case PixelFormat::RGB565: {  // by name only
                const Rgb c = rgb565(loadU16LE(s + 2 * i));
                d[0] = c.r, d[1] = c.g, d[2] = c.b, d[3] = 255;
                break;
            }
            case PixelFormat::RGB555: {  // by name only
                const unsigned v = loadU16LE(s + 2 * i);
                d[0] = expand((v >> 10) & 31u, 5), d[1] = expand((v >> 5) & 31u, 5), d[2] = expand(v & 31u, 5),
                d[3] = 255;
                break;
            }
            default:
                throw ContentError(std::string("cannot decode pixel format ") + pixelFormatName(f));
        }
    }
    return out;
}

}  // namespace trainz::content
