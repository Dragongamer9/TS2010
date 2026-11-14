#include "TextureDescriptor.h"

#include "ContentError.h"

#include <array>
#include <charconv>
#include <utility>

namespace trainz::content {

namespace {

constexpr std::size_t kMaxDescriptorSize = 64 * 1024;

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

template <typename E, std::size_t N>
using WordTable = std::array<std::pair<const char*, E>, N>;

// The word tables as stored in RenderSystem_7c.dll (VA 0x1006ca78-0x1006cb34).
constexpr WordTable<TexDimension, 4> kDimension = {
    {{"two", TexDimension::Two}, {"cube", TexDimension::Cube}, {"volume", TexDimension::Volume}, {"one", TexDimension::One}}};
constexpr WordTable<TexCompression, 4> kCompression = {{{"none", TexCompression::None},
                                                        {"dxt1", TexCompression::Dxt1},
                                                        {"dxt3", TexCompression::Dxt3},
                                                        {"dxt5", TexCompression::Dxt5}}};
constexpr WordTable<TexFilter, 3> kFilter = {
    {{"nearest", TexFilter::Nearest}, {"linear", TexFilter::Linear}, {"default", TexFilter::Default}}};
constexpr WordTable<TexNormalMapHint, 2> kNormalMapHint = {
    {{"none", TexNormalMapHint::None}, {"normalmap", TexNormalMapHint::NormalMap}}};
constexpr WordTable<TexModifyMap, 2> kModifyMap = {{{"none", TexModifyMap::None}, {"flipgreen", TexModifyMap::FlipGreen}}};
constexpr WordTable<TexAlphaHint, 3> kAlphaHint = {{{"opaque", TexAlphaHint::Opaque},
                                                    {"semitransparent", TexAlphaHint::SemiTransparent},
                                                    {"masked", TexAlphaHint::Masked}}};
constexpr WordTable<TexHint, 2> kHint = {{{"static", TexHint::Static}, {"dynamic", TexHint::Dynamic}}};
constexpr WordTable<TexAutoMip, 4> kAutoMip = {{{"none", TexAutoMip::None},
                                                {"default", TexAutoMip::Default},
                                                {"fastest", TexAutoMip::Fastest},
                                                {"nicest", TexAutoMip::Nicest}}};

template <typename E, std::size_t N>
void readWord(const WordTable<E, N>& table, std::string_view key, std::string_view value, std::optional<E>& field,
              std::vector<std::string>& warnings) {
    if (field) {
        return;  // the game reads the first occurrence (ConfigData::Find)
    }
    const std::string v = lower(value);
    for (const auto& [word, e] : table) {
        if (v == word) {
            field = e;
            return;
        }
    }
    warnings.push_back(std::string(key) + ": unknown value '" + std::string(value) +
                       "'; the game keeps its default (as does this reader)");
}

}  // namespace

TextureDescriptor parseTextureDescriptor(std::span<const std::uint8_t> data) {
    if (data.size() > kMaxDescriptorSize) {
        throw ContentError("texture descriptor is " + std::to_string(data.size()) + " bytes; over the 64 KiB limit");
    }
    for (const std::uint8_t b : data) {
        if (b == 0) {
            throw ContentError("texture descriptor contains NUL bytes; not a text file");
        }
    }
    std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
    if (text.substr(0, 3) == "\xEF\xBB\xBF") {
        text.remove_prefix(3);
    }
    TextureDescriptor d;
    std::size_t lineNo = 0;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        ++lineNo;
        if (line.empty() || line.substr(0, 2) == "//") {
            continue;
        }
        // "Key=value" in every disc file; a space instead of '=' is accepted too.
        std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) {
            sep = line.find_first_of(" \t");
        }
        const std::string_view key = trim(line.substr(0, sep));
        std::string_view value = sep == std::string_view::npos ? std::string_view{} : trim(line.substr(sep + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        const std::string k = lower(key);
        if (k == "primary") {
            d.primary.emplace_back(value);
        } else if (k == "alpha") {
            d.alpha.emplace_back(value);
        } else if (k == "dimension") {
            readWord(kDimension, key, value, d.dimension, d.warnings);
        } else if (k == "compression") {
            readWord(kCompression, key, value, d.compression, d.warnings);
        } else if (k == "hint") {
            readWord(kHint, key, value, d.hint, d.warnings);
        } else if (k == "normalmaphint") {
            readWord(kNormalMapHint, key, value, d.normalMapHint, d.warnings);
        } else if (k == "modifymap") {
            readWord(kModifyMap, key, value, d.modifyMap, d.warnings);
        } else if (k == "alphahint") {
            readWord(kAlphaHint, key, value, d.alphaHint, d.warnings);
        } else if (k == "automip") {
            readWord(kAutoMip, key, value, d.autoMip, d.warnings);
        } else if (k == "magfilter") {
            readWord(kFilter, key, value, d.magFilter, d.warnings);
        } else if (k == "minfilter") {
            readWord(kFilter, key, value, d.minFilter, d.warnings);
        } else if (k == "mipfilter") {
            readWord(kFilter, key, value, d.mipFilter, d.warnings);
        } else if (k == "anisotropy") {
            if (!d.anisotropy) {
                std::int32_t n = 0;
                const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
                if (ec == std::errc{} && end == value.data() + value.size()) {
                    d.anisotropy = n;
                } else {
                    d.warnings.push_back("Anisotropy: '" + std::string(value) + "' is not a whole number; ignored");
                }
            }
        } else if (k == "tile") {
            if (!d.wrapS) {
                const std::string v = lower(value);
                d.wrapS = v.find('s') != std::string::npos;
                d.wrapT = v.find('t') != std::string::npos;
            }
        } else {
            d.warnings.push_back("line " + std::to_string(lineNo) + ": unknown key '" + std::string(key) + "'; ignored");
        }
    }
    if (d.primary.empty()) {
        d.warnings.push_back("no Primary image");
    }
    if (d.primary.size() > 1 || d.alpha.size() > 1) {
        d.warnings.push_back("more than one Primary/Alpha (cube or volume textures?); only the first is used here");
    }
    return d;
}

std::string resolveDescriptorPath(std::string_view descriptorName, std::string_view imageName) {
    const std::size_t slash = descriptorName.find_last_of("/\\");
    if (slash == std::string_view::npos) {
        return std::string(imageName);
    }
    return std::string(descriptorName.substr(0, slash + 1)) + std::string(imageName);
}

RgbaImage composeDescriptorImage(const RgbaImage& primary, const RgbaImage* alpha, bool alphaIsSameFile) {
    RgbaImage out = primary;
    out.warnings.clear();
    if (alpha == nullptr) {
        for (std::size_t i = 3; i < out.rgba.size(); i += 4) {
            out.rgba[i] = 255;
        }
        out.alphaFromFile = false;
        return out;
    }
    if (alphaIsSameFile) {
        return out;
    }
    if (alpha->width != primary.width || alpha->height != primary.height) {
        throw ContentError("Alpha image is " + std::to_string(alpha->width) + "x" + std::to_string(alpha->height) +
                           " but Primary is " + std::to_string(primary.width) + "x" + std::to_string(primary.height) +
                           "; they must match");
    }
    for (std::size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
        out.rgba[i + 3] = alpha->rgba[i];  // grey level (red channel)
    }
    out.alphaFromFile = true;
    return out;
}

}  // namespace trainz::content
