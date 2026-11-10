#include "JirfFile.h"

#include "ByteOrder.h"
#include "ContentError.h"

#include <cstring>

namespace trainz::content {

namespace {

bool printableTag(const std::uint8_t* p) {
    for (int i = 0; i < 4; ++i) {
        if (p[i] < 0x20 || p[i] > 0x7E) {
            return false;
        }
    }
    return true;
}

}  // namespace

const JirfChunk* JirfFile::first(std::string_view tag) const {
    for (const auto& c : chunks) {
        if (c.tag == tag) {
            return &c;
        }
    }
    return nullptr;
}

std::vector<JirfChunk> parseJirfChunks(std::span<const std::uint8_t> data, std::size_t baseOffset) {
    std::vector<JirfChunk> out;
    std::size_t pos = 0;
    while (pos < data.size()) {
        if (data.size() - pos < 8) {
            throw ContentError("JIRF chunk header at byte " + std::to_string(baseOffset + pos) + " is cut short");
        }
        const std::uint8_t* p = data.data() + pos;
        if (!printableTag(p)) {
            throw ContentError("JIRF chunk at byte " + std::to_string(baseOffset + pos) + " has an unreadable tag");
        }
        const std::uint32_t size = loadU32LE(p + 4);
        if (size > data.size() - pos - 8) {
            throw ContentError("JIRF chunk '" + std::string(reinterpret_cast<const char*>(p), 4) + "' at byte " +
                               std::to_string(baseOffset + pos) + " runs past the end");
        }
        JirfChunk c;
        c.tag.assign(reinterpret_cast<const char*>(p), 4);
        c.offset = baseOffset + pos + 8;
        c.data = data.subspan(pos + 8, size);
        out.push_back(std::move(c));
        pos += 8 + static_cast<std::size_t>(size);
    }
    return out;
}

JirfFile parseJirf(std::span<const std::uint8_t> data) {
    if (data.size() < 12) {
        throw ContentError("too small to be a JIRF file (" + std::to_string(data.size()) + " bytes)");
    }
    if (std::memcmp(data.data(), "JIRF", 4) != 0) {
        throw ContentError("not a JIRF file (no \"JIRF\" at the start)");
    }
    const std::uint32_t size = loadU32LE(data.data() + 4);
    if (size < 4 || size > data.size() - 8) {
        throw ContentError("JIRF size field (" + std::to_string(size) + ") does not fit the file (" +
                           std::to_string(data.size()) + " bytes)");
    }
    JirfFile f;
    if (!printableTag(data.data() + 8)) {
        throw ContentError("JIRF form type is unreadable");
    }
    f.form.assign(reinterpret_cast<const char*>(data.data() + 8), 4);
    if (data.size() - 8 > size) {
        f.warnings.push_back(std::to_string(data.size() - 8 - size) + " bytes after the JIRF data are ignored");
    }
    f.chunks = parseJirfChunks(data.subspan(12, size - 4), 12);
    return f;
}

}  // namespace trainz::content
