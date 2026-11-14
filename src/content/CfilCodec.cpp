#include "CfilCodec.h"

#include "ByteOrder.h"
#include "ContentError.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace trainz::content {

namespace {

constexpr std::uint8_t kMagic[4] = {'C', 'F', 'I', 'L'};

// The original keeps its expansion stack in a 256-byte buffer on the stack
// (esp+0x120 .. esp+0x220 in 0x1000d130) and does not check it. We keep the
// same size but refuse to overflow it.
constexpr std::size_t kBpeStackSize = 256;

}  // namespace

bool hasCfilMagic(std::span<const std::uint8_t> data) {
    return data.size() >= 4 && std::memcmp(data.data(), kMagic, 4) == 0;
}

CfilHeader parseCfilHeader(std::span<const std::uint8_t> data, std::uint32_t archiveVersion,
                           std::uint32_t entryOriginalSize) {
    if (!hasCfilMagic(data)) {
        throw ContentError("CFIL data does not start with the \"CFIL\" magic");
    }
    CfilHeader h;
    if (archiveVersion == 0x101) {
        // 0x10020038: for version 0x101 archives the reader takes only 4 bytes.
        h.headerSize = 4;
        h.originalSize = entryOriginalSize;
        return h;
    }
    // 0x10020029/0x10020031: otherwise it reads 12 bytes: magic, version, size.
    if (data.size() < 12) {
        throw ContentError("CFIL header is cut short (" + std::to_string(data.size()) + " of 12 bytes)");
    }
    h.headerSize = 12;
    h.version = loadU32LE(data.data() + 4);
    // 0x10020084: the size field is used only when version >= 0x101.
    if (h.version >= 0x101) {
        h.originalSize = loadU32LE(data.data() + 8);
        h.sizeFromHeader = true;
    } else {
        h.originalSize = entryOriginalSize;
    }
    return h;
}

void expandBpeBlock(std::span<const std::uint8_t> in, std::vector<std::uint8_t>& out,
                    std::size_t maxOutputSize) {
    std::size_t pos = 0;
    auto need = [&](std::size_t n) {
        if (in.size() - pos < n) {
            throw ContentError("BPE block ends early at byte " + std::to_string(pos) + " of " +
                               std::to_string(in.size()));
        }
    };

    std::array<std::uint8_t, 256> left{};
    std::array<std::uint8_t, 256> right{};
    std::array<std::uint8_t, kBpeStackSize> stack{};

    // One block payload may hold several BPE sub-blocks back to back
    // (0x1000d271: loop while input remains).
    while (pos < in.size()) {
        // Every sub-block starts from the identity table (0x1000d170 / rep movs).
        for (std::size_t i = 0; i < 256; ++i) {
            left[i] = static_cast<std::uint8_t>(i);
            right[i] = 0;
        }

        // Pair table. A count byte > 127 skips (count - 127) codes. Unlike
        // Gage's original BPE, a skip does not also read a pair (0x1000d1a6 jumps
        // straight to the next count). Otherwise (count + 1) pairs follow; the
        // right byte is present only when left != code.
        unsigned code = 0;
        need(1);
        unsigned count = in[pos++];
        for (;;) {
            if (count > 127) {
                code += count - 127;
            } else {
                for (unsigned i = 0; i <= count; ++i) {
                    if (code >= 256) {
                        throw ContentError("BPE pair table runs past 256 codes");
                    }
                    need(1);
                    left[code] = in[pos++];
                    if (left[code] != static_cast<std::uint8_t>(code)) {
                        need(1);
                        right[code] = in[pos++];
                    }
                    ++code;
                }
            }
            if (code >= 256) {  // 0x1000d1ec: cmp ecx,0x100 / jge
                break;
            }
            need(1);
            count = in[pos++];
        }

        // Big-endian byte count of the packed data (0x1000d1f6).
        need(2);
        const std::size_t packed = (static_cast<std::size_t>(in[pos]) << 8) | in[pos + 1];
        pos += 2;
        need(packed);
        const std::size_t end = pos + packed;

        // Expand: pop from the stack first, otherwise take the next input byte.
        // A literal (left[b] == b) is written out; a pair pushes right then left
        // so left comes out first.
        std::size_t depth = 0;
        for (;;) {
            unsigned b;
            if (depth > 0) {
                b = stack[--depth];
            } else {
                if (pos == end) {
                    break;
                }
                b = in[pos++];
            }
            if (left[b] == static_cast<std::uint8_t>(b)) {
                if (out.size() >= maxOutputSize) {
                    throw ContentError("BPE block expands to more than the allowed " +
                                       std::to_string(maxOutputSize) + " bytes");
                }
                out.push_back(static_cast<std::uint8_t>(b));
            } else {
                if (depth + 2 > stack.size()) {
                    throw ContentError("BPE pair nesting is too deep (damaged pair table)");
                }
                stack[depth++] = right[b];
                stack[depth++] = left[b];
            }
        }
    }
}

std::vector<std::uint8_t> decodeCfil(std::span<const std::uint8_t> data, std::uint32_t archiveVersion,
                                     std::uint32_t entryOriginalSize) {
    const CfilHeader h = parseCfilHeader(data, archiveVersion, entryOriginalSize);
    std::vector<std::uint8_t> out;
    // Do not trust the header for the allocation: a damaged size field could ask for 4 GiB.
    out.reserve(std::min<std::size_t>(h.originalSize, std::size_t{1} << 20));
    std::size_t pos = h.headerSize;
    while (out.size() < h.originalSize) {
        if (data.size() - pos < 2) {
            throw ContentError("CFIL data ends before all " + std::to_string(h.originalSize) +
                               " bytes were produced (" + std::to_string(out.size()) + " so far)");
        }
        const std::uint16_t header = loadU16LE(data.data() + pos);
        pos += 2;
        const std::size_t remaining = h.originalSize - out.size();
        if (header & kCfilRawFlag) {
            const std::size_t len = header & 0x7FFF;
            if (len == 0) {
                throw ContentError("CFIL raw block of length 0 at byte " + std::to_string(pos - 2));
            }
            if (data.size() - pos < len) {
                throw ContentError("CFIL raw block at byte " + std::to_string(pos - 2) + " runs past the end");
            }
            if (len > remaining || len > kCfilBlockSize) {
                throw ContentError("CFIL raw block at byte " + std::to_string(pos - 2) + " is too long");
            }
            out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(pos),
                       data.begin() + static_cast<std::ptrdiff_t>(pos + len));
            pos += len;
        } else {
            const std::size_t len = header;
            if (len == 0) {
                throw ContentError("CFIL packed block of length 0 at byte " + std::to_string(pos - 2));
            }
            if (data.size() - pos < len) {
                throw ContentError("CFIL packed block at byte " + std::to_string(pos - 2) + " runs past the end");
            }
            const std::size_t before = out.size();
            const std::size_t limit = before + std::min(remaining, kCfilBlockSize);
            expandBpeBlock(data.subspan(pos, len), out, limit);
            if (out.size() == before) {
                throw ContentError("CFIL packed block at byte " + std::to_string(pos - 2) + " produced no data");
            }
            pos += len;
        }
    }
    return out;
}

}  // namespace trainz::content
