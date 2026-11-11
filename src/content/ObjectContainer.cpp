#include "ObjectContainer.h"

#include "ByteOrder.h"
#include "ContentError.h"

#include <array>
#include <utility>

namespace trainz::content {

namespace {

constexpr std::uint32_t kMaxItemSize = 0xFFFFFF;  // 0x8869b8

std::uint32_t takeU32(std::span<const std::uint8_t> d, std::size_t& pos) {
    if (d.size() < 4 || pos > d.size() - 4) {
        throw ContentError("object container ends early at byte " + std::to_string(pos) + " of " +
                           std::to_string(d.size()));
    }
    const std::uint32_t v = loadU32LE(d.data() + pos);
    pos += 4;
    return v;
}

std::vector<std::uint8_t> takeBytes(std::span<const std::uint8_t> d, std::size_t& pos, std::size_t n) {
    if (n > d.size() || pos > d.size() - n) {
        throw ContentError("object container needs " + std::to_string(n) + " bytes at " + std::to_string(pos) +
                           " but the data ends at " + std::to_string(d.size()));
    }
    std::vector<std::uint8_t> v(d.begin() + static_cast<std::ptrdiff_t>(pos),
                                d.begin() + static_cast<std::ptrdiff_t>(pos + n));
    pos += n;
    return v;
}

// Tag and class name pairs from the registrations at 0x807eb0 (tag pushed with the
// name string next to it). Tags are written here in readable order.
constexpr std::array<std::pair<std::string_view, std::string_view>, 17> kClassNames = {{
    {"BHVR", "behavior"},
    {"scOb", "scenery"},
    {"mOBJ", "mapobject"},
    {"mobs", "mapobjectstub"},
    {"tsOb", "trackside"},
    {"flOb", "floatscenery"},
    {"mkOb", "motrackmark"},
    {"ttOb", "turntable"},
    {"tgOb", "motrigger"},
    {"obCr", "mocrossing"},
    {"jnOb", "mojunction"},
    {"sgOb", "mosignal"},
    {"spOb", "mospeedboard"},
    {"mIND", "industry"},
    {"mPAS", "passenger-station"},
    {"mBUI", "buildable"},
    {"mFXT", "fixedtrack"},
}};

}  // namespace

std::string_view objectClassName(std::string_view tag) {
    for (const auto& [t, name] : kClassNames) {
        if (t == tag) {
            return name;
        }
    }
    return {};
}

ObjectContainer readObjectContainer(std::span<const std::uint8_t> data, std::size_t& pos) {
    ObjectContainer c;
    c.version = takeU32(data, pos);
    if (c.version < 1 || c.version > 5) {
        return c;  // the game reads nothing more for other versions
    }
    if (c.version >= 5) {
        c.word = takeU32(data, pos);  // 0x886c82
    }
    if (c.version == 2) {
        throw ContentError("object container version 2 is not handled (items are not self-sized)");
    }
    if (c.version >= 3) {
        for (;;) {  // 0x886800: items until an index of -1
            ObjectItem it;
            it.index = static_cast<std::int32_t>(takeU32(data, pos));
            if (it.index == -1) {
                break;
            }
            const std::uint32_t tag = takeU32(data, pos);
            for (int k = 3; k >= 0; --k) {
                it.tag.push_back(static_cast<char>((tag >> (8 * k)) & 0xFF));
            }
            const auto content = static_cast<std::int32_t>(takeU32(data, pos));
            it.kuid = unpackChumpKuid(takeU32(data, pos), content);
            const std::uint32_t size = takeU32(data, pos);
            if (size > kMaxItemSize) {
                throw ContentError("object " + std::to_string(it.index) + " (" + it.tag + ") claims " +
                                   std::to_string(size) + " bytes; over the game's 16 MiB limit");
            }
            it.payload = takeBytes(data, pos, size);
            c.items.push_back(std::move(it));
        }
    }
    if (c.version >= 4) {
        const std::uint32_t size = takeU32(data, pos);  // 0x8858d0: u32 size, then size bytes
        c.tail = takeBytes(data, pos, size);
    }
    return c;
}

RouteObjectFile parseRouteObjectFile(std::span<const std::uint8_t> data) {
    RouteObjectFile f;
    std::size_t pos = 0;
    f.fileWord = takeU32(data, pos);
    while (pos < data.size()) {
        const std::size_t start = pos;
        ObjectContainer c = readObjectContainer(data, pos);
        if (c.version < 1 || c.version > 5) {
            throw ContentError("container at byte " + std::to_string(start) + " has version " +
                               std::to_string(c.version) + " (1..5 expected)");
        }
        f.containers.push_back(std::move(c));
    }
    return f;
}

}  // namespace trainz::content
