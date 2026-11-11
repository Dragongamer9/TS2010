#include "SessionFile.h"

#include "ByteOrder.h"
#include "ContentError.h"

namespace trainz::content {

namespace {

std::size_t blocksForVersion(std::uint32_t v) {
    switch (v) {
        case 0: return 3;
        case 1: return 4;
        case 2:
        case 3: return 5;
        default: return 0;
    }
}

}  // namespace

SessionProfile parseSessionProfile(std::span<const std::uint8_t> data) {
    if (data.size() < 4) {
        throw ContentError("profile.dat is too short");
    }
    SessionProfile p;
    p.version = loadU32LE(data.data());
    const std::size_t count = blocksForVersion(p.version);
    if (count == 0) {
        throw ContentError("profile.dat version " + std::to_string(p.version) + " is not known (0..3 seen)");
    }
    std::size_t pos = 4;
    for (std::size_t k = 0; k < count; ++k) {
        if (data.size() - pos < 4) {
            throw ContentError("profile.dat ends before block " + std::to_string(k));
        }
        const std::uint32_t size = loadU32LE(data.data() + pos);
        pos += 4;
        if (size > data.size() - pos) {
            throw ContentError("profile.dat block " + std::to_string(k) + " of " + std::to_string(size) +
                               " bytes runs past the end");
        }
        SessionBlock b;
        b.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(pos),
                       data.begin() + static_cast<std::ptrdiff_t>(pos + size));
        pos += size;
        // A block is a container if one container reads to exactly its end.
        try {
            std::size_t cpos = 0;
            ObjectContainer c = readObjectContainer(b.bytes, cpos);
            if (cpos == b.bytes.size() && c.version >= 1 && c.version <= 5) {
                b.container = std::move(c);
            }
        } catch (const ContentError&) {
            // not a container
        }
        p.blocks.push_back(std::move(b));
    }
    p.trailer.assign(data.begin() + static_cast<std::ptrdiff_t>(pos), data.end());
    if (p.version == 3 && p.trailer.size() != 44) {
        p.warnings.push_back("version 3 trailer is " + std::to_string(p.trailer.size()) + " bytes (44 on the disc)");
    } else if (p.version != 3 && !p.trailer.empty()) {
        p.warnings.push_back(std::to_string(p.trailer.size()) + " bytes after the last block");
    }
    return p;
}

std::optional<Behaviour> decodeBehaviour(const ObjectItem& item) {
    const auto& d = item.payload;
    if (item.tag != "BHVR" || d.size() < 12) {
        return std::nullopt;
    }
    Behaviour b;
    b.index = item.index;
    b.kuid = item.kuid;
    b.version = loadU32LE(d.data());
    if (loadU32LE(d.data() + 4) != d.size() - 8) {
        return std::nullopt;
    }
    std::size_t pos = 0;
    if (b.version == 3 && d.size() >= 17 && loadU32LE(d.data() + 8) == 1 && d[12] == 0) {
        pos = 13;
    } else if (b.version == 2) {
        pos = 8;
    } else {
        return std::nullopt;
    }
    const std::uint32_t nameLength = loadU32LE(d.data() + pos);
    pos += 4;
    if (nameLength > d.size() - pos || d.size() - pos - nameLength < 4) {
        return std::nullopt;
    }
    std::string name(reinterpret_cast<const char*>(d.data() + pos), nameLength);
    if (const auto nul = name.find('\0'); nul != std::string::npos) {
        name.resize(nul);
    }
    b.name = std::move(name);
    pos += nameLength + 4;  // the name, then a u32 0
    try {
        b.settings = parseChump(std::span(d).subspan(pos));
    } catch (const ContentError&) {
        return std::nullopt;
    }
    return b;
}

}  // namespace trainz::content
