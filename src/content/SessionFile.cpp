#include "SessionFile.h"

#include "ByteOrder.h"
#include "ContentError.h"

#include <cmath>
#include <cstring>

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

namespace {

// Old (pre-version-5) trains: see SessionFile.h. Vehicles are found by a pattern.
std::optional<Train> decodeOldTrain(const ObjectItem& item) {
    const auto& d = item.payload;
    if (d.size() < 20 || loadU32LE(d.data() + 4) != 0) {
        return std::nullopt;
    }
    const std::uint32_t count = loadU32LE(d.data());
    if (count == 0 || count > d.size() / 61) {
        return std::nullopt;
    }
    // 5 zero bytes, then the record's u32 version (4; 1 or 5 in a few) and u32 3.
    static constexpr std::uint8_t kMark[13] = {0, 0, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0};
    std::vector<std::size_t> starts;
    for (std::size_t i = 8; i + sizeof kMark <= d.size(); ++i) {
        const std::uint8_t ver = d[i + 5];
        if ((ver == 1 || ver == 4 || ver == 5) && std::memcmp(d.data() + i, kMark, 5) == 0 &&
            std::memcmp(d.data() + i + 6, kMark + 6, sizeof kMark - 6) == 0) {
            starts.push_back(i - 8);
        }
    }
    if (starts.size() != count || starts[0] != 20) {
        return std::nullopt;
    }
    Train t;
    t.index = item.index;
    t.oldLayout = true;
    t.trackPiece = static_cast<std::int32_t>(loadU32LE(d.data() + 8));
    std::memcpy(&t.distance, d.data() + 12, 4);
    t.direction = loadU32LE(d.data() + 16);
    for (std::size_t k = 0; k < starts.size(); ++k) {
        const std::size_t s = starts[k];
        const std::size_t end = k + 1 < starts.size() ? starts[k + 1] : d.size();
        if (end < s + 57) {
            return std::nullopt;
        }
        Vehicle v;
        v.kuid = unpackChumpKuid(loadU32LE(d.data() + s + 4), static_cast<std::int32_t>(loadU32LE(d.data() + s)));
        v.record.assign(d.begin() + static_cast<std::ptrdiff_t>(s + 13), d.begin() + static_cast<std::ptrdiff_t>(end));
        const auto& r = v.record;
        v.recordVersion = loadU32LE(r.data());
        v.boardX = static_cast<std::int16_t>(loadU16LE(r.data() + 12));
        v.boardY = static_cast<std::int16_t>(loadU16LE(r.data() + 14));
        std::memcpy(&v.x, r.data() + 16, 4);
        std::memcpy(&v.y, r.data() + 20, 4);
        std::memcpy(&v.z, r.data() + 24, 4);
        std::memcpy(&v.yaw, r.data() + 28, 4);
        const std::uint32_t len = loadU32LE(r.data() + 40);
        if (len == 0 || len > r.size() - 44 || r[44 + len - 1] != 0) {
            return std::nullopt;  // the pattern found something that isn't a vehicle
        }
        v.name.assign(reinterpret_cast<const char*>(r.data() + 44), len - 1);
        // Disc-tested heuristic, not a format rule: all 3,363 size-checked version-5
        // vehicles lie inside their baseboard (x and y in 0..720).
        if (!(v.x >= 0 && v.x < 720 && v.y >= 0 && v.y < 720)) {
            return std::nullopt;
        }
        if (!t.vehicles.empty()) {
            // Disc-tested heuristic, not a format rule: no accepted train in the surveyed disc
            // data (nor any size-checked version-5 train) has neighbours 50 m or more apart.
            const auto& u = t.vehicles.back();
            const double gap = std::hypot((v.boardX - u.boardX) * 720.0 + v.x - u.x, (v.boardY - u.boardY) * 720.0 + v.y - u.y);
            if (!(gap < 50.0)) {
                return std::nullopt;
            }
        }
        t.vehicles.push_back(std::move(v));
    }
    return t;
}

}  // namespace

std::optional<Train> decodeTrain(const ObjectItem& item) {
    const auto& d = item.payload;
    if (item.tag != "trCt") {
        return std::nullopt;
    }
    if (d.size() >= 4 && loadU32LE(d.data()) != 0xCAFFA1DAu) {
        return decodeOldTrain(item);
    }
    if (d.size() < 28 || loadU32LE(d.data() + 4) != 5) {
        return std::nullopt;
    }
    const std::uint32_t count = loadU32LE(d.data() + 8);
    if (count > d.size() / 17) {
        return std::nullopt;
    }
    Train t;
    t.index = item.index;
    t.trackPiece = static_cast<std::int32_t>(loadU32LE(d.data() + 16));
    std::memcpy(&t.distance, d.data() + 20, 4);
    t.direction = loadU32LE(d.data() + 24);
    std::size_t pos = 28;
    for (std::uint32_t k = 0; k < count; ++k) {
        if (d.size() - pos < 17) {
            return std::nullopt;
        }
        Vehicle v;
        v.kuid = unpackChumpKuid(loadU32LE(d.data() + pos + 4), static_cast<std::int32_t>(loadU32LE(d.data() + pos)));
        const std::uint32_t size = loadU32LE(d.data() + pos + 13);
        pos += 17;
        if (size > d.size() - pos) {
            return std::nullopt;
        }
        v.record.assign(d.begin() + static_cast<std::ptrdiff_t>(pos), d.begin() + static_cast<std::ptrdiff_t>(pos + size));
        pos += size;
        const auto& r = v.record;
        if (r.size() >= 46 && r[41] == 0x80) {
            v.recordVersion = loadU32LE(r.data());
            v.flag = r[12];
            v.boardX = static_cast<std::int16_t>(loadU16LE(r.data() + 13));
            v.boardY = static_cast<std::int16_t>(loadU16LE(r.data() + 15));
            std::memcpy(&v.x, r.data() + 17, 4);
            std::memcpy(&v.y, r.data() + 21, 4);
            std::memcpy(&v.z, r.data() + 25, 4);
            std::memcpy(&v.yaw, r.data() + 29, 4);
            const std::uint32_t len = loadU32LE(r.data() + 42);
            if (len > 0 && len <= r.size() - 46) {
                std::string name(reinterpret_cast<const char*>(r.data() + 46), len);
                if (const auto nul = name.find('\0'); nul != std::string::npos) {
                    name.resize(nul);
                }
                v.name = std::move(name);
            }
        }
        t.vehicles.push_back(std::move(v));
    }
    t.tail.assign(d.begin() + static_cast<std::ptrdiff_t>(pos), d.end());
    return t;
}

}  // namespace trainz::content
