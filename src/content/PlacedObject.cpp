#include "PlacedObject.h"

#include "ByteOrder.h"
#include "TrackLayout.h"  // kBaseboardSize

#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

namespace trainz::content {

namespace {

float f32At(const std::uint8_t* p) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
}

constexpr std::size_t kBaseSize = 1 + 2 + 2 + 4 * 4;

// Classes seen on the disc with the base block after an 8-byte header.
constexpr std::array<std::string_view, 10> kEightByteHeader = {"mOBJ", "sgOb", "jnOb", "tsO2", "cmOb",
                                                               "obCr", "mIND", "mFXT", "mBUI", "ttOb"};

}  // namespace

double PlacedObject::worldX() const { return boardX * kBaseboardSize + x; }
double PlacedObject::worldY() const { return boardY * kBaseboardSize + y; }

std::optional<std::size_t> placedObjectHeaderSize(const std::string& tag) {
    if (tag == "mobs") {
        return 0;
    }
    for (const auto t : kEightByteHeader) {
        if (tag == t) {
            return 8;
        }
    }
    return std::nullopt;
}

std::optional<PlacedObject> decodePlacedObject(const ObjectItem& item) {
    const auto header = placedObjectHeaderSize(item.tag);
    if (!header || item.payload.size() < *header + kBaseSize) {
        return std::nullopt;
    }
    const std::uint8_t* p = item.payload.data() + *header;
    PlacedObject o;
    o.index = item.index;
    o.tag = item.tag;
    o.kuid = item.kuid;
    o.flag = p[0];
    o.boardX = static_cast<std::int16_t>(loadU16LE(p + 1));
    o.boardY = static_cast<std::int16_t>(loadU16LE(p + 3));
    o.x = f32At(p + 5);
    o.y = f32At(p + 9);
    o.z = f32At(p + 13);
    o.yaw = f32At(p + 17);
    if (o.flag > 1 || !(o.x >= 0 && o.x < kBaseboardSize && o.y >= 0 && o.y < kBaseboardSize) || !std::isfinite(o.z)) {
        return std::nullopt;
    }
    o.rest.assign(item.payload.begin() + static_cast<std::ptrdiff_t>(*header + kBaseSize), item.payload.end());
    return o;
}

}  // namespace trainz::content
