#include "GroundTextures.h"

#include "content/AssetConfig.h"
#include "content/ContentError.h"
#include "content/ImageFile.h"
#include "content/TextureDescriptor.h"
#include "content/TextureFile.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trainz::view {

using namespace trainz::content;

namespace {

constexpr std::size_t kMaxFileBytes = std::size_t{64} << 20;

// Halves `t` until both sides fit `side`, then appends the rest of the chain to `smaller`.
SceneTexture withMips(SceneTexture t, std::uint32_t side) {
    while (t.width > side || t.height > side) {
        t = halveTexture(t);
    }
    SceneTexture level = t;
    while (level.width > 1 || level.height > 1) {
        level = halveTexture(level);
        SceneTexture copy = level;
        copy.smaller.clear();
        t.smaller.push_back(std::move(copy));
    }
    return t;
}

}  // namespace

SceneTexture halveTexture(const SceneTexture& t) {
    SceneTexture out;
    out.alphaTest = t.alphaTest;
    out.clampEdges = t.clampEdges;
    out.width = std::max<std::uint32_t>(1, t.width / 2);
    out.height = std::max<std::uint32_t>(1, t.height / 2);
    if (t.width == 0 || t.height == 0 || t.rgba.size() < std::size_t{t.width} * t.height * 4) {
        out.width = out.height = 0;
        return out;
    }
    out.rgba.resize(std::size_t{out.width} * out.height * 4);
    for (std::uint32_t y = 0; y < out.height; ++y) {
        for (std::uint32_t x = 0; x < out.width; ++x) {
            for (int c = 0; c < 4; ++c) {
                unsigned sum = 0, n = 0;
                for (std::uint32_t dy = 0; dy < 2; ++dy) {
                    for (std::uint32_t dx = 0; dx < 2; ++dx) {
                        const std::uint32_t sx = std::min(t.width - 1, 2 * x + dx), sy = std::min(t.height - 1, 2 * y + dy);
                        sum += t.rgba[(std::size_t{sy} * t.width + sx) * 4 + static_cast<std::size_t>(c)];
                        ++n;
                    }
                }
                out.rgba[(std::size_t{y} * out.width + x) * 4 + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(sum / n);
            }
        }
    }
    return out;
}

SceneTexture bakeGroundSection(const GroundSectionData& data, std::size_t side,
                               const std::vector<const SceneTexture*>& slotTexture, std::uint32_t size,
                               const std::function<Rgb(double col, double row)>& fallback, double tileCells) {
    if (size == 0 || size > 4096 || side < 8 || side > 1024 || data.vertices.size() != side * side ||
        !(tileCells > 0) || !std::isfinite(tileCells)) {
        throw std::invalid_argument("bakeGroundSection: bad size, side, vertex count or tile size");
    }
    const std::size_t cells = side - 4;
    const double pxPerTile = static_cast<double>(size) / static_cast<double>(cells) * tileCells;
    // Per slot: the mip whose width is closest to (not below) one texel per baked pixel.
    std::vector<const SceneTexture*> level(slotTexture.size(), nullptr);
    for (std::size_t k = 0; k < slotTexture.size(); ++k) {
        const SceneTexture* t = slotTexture[k];
        if (t == nullptr || t->width == 0 || t->height == 0 || t->rgba.size() < std::size_t{t->width} * t->height * 4) {
            continue;
        }
        const SceneTexture* pick = t;
        for (const auto& m : t->smaller) {
            if (m.width < pxPerTile || m.width == 0 || m.height == 0 || m.rgba.size() < std::size_t{m.width} * m.height * 4) {
                break;
            }
            pick = &m;
        }
        level[k] = pick;
    }
    SceneTexture out;
    out.width = out.height = size;
    out.clampEdges = true;  // one section, not a repeating pattern
    out.rgba.resize(std::size_t{size} * size * 4);
    for (std::uint32_t py = 0; py < size; ++py) {
        for (std::uint32_t px = 0; px < size; ++px) {
            const double fc = (px + 0.5) / size * static_cast<double>(cells);
            const double fr = (py + 0.5) / size * static_cast<double>(cells);
            const std::size_t c0 = std::min(cells - 1, static_cast<std::size_t>(fc));
            const std::size_t r0 = std::min(cells - 1, static_cast<std::size_t>(fr));
            const double tx = fc - static_cast<double>(c0), ty = fr - static_cast<double>(r0);
            // Repeating texture position (u, v) in texture widths; image row 0 at the tile's -y side.
            const double u = fc / tileCells, v = fr / tileCells;
            double acc[3] = {0, 0, 0}, wsum = 0;
            for (int corner = 0; corner < 4; ++corner) {
                const std::size_t dc = static_cast<std::size_t>(corner & 1), dr = static_cast<std::size_t>(corner >> 1);
                const double bw = (dc ? tx : 1 - tx) * (dr ? ty : 1 - ty);
                if (bw <= 0) {
                    continue;
                }
                const auto& vert = data.vertices[(r0 + dr) * side + (c0 + dc)];
                for (int l = 0; l < 3; ++l) {
                    const std::uint8_t k = vert.texture[l];
                    if (k == 0xFF || k >= level.size() || level[k] == nullptr || vert.weight[l] == 0) {
                        continue;
                    }
                    const SceneTexture& t = *level[k];
                    double texel[3];
                    sampleTextureBilinear(t, u - std::floor(u), v - std::floor(v), texel);
                    const double w = bw * vert.weight[l];
                    acc[0] += w * texel[0], acc[1] += w * texel[1], acc[2] += w * texel[2];
                    wsum += w;
                }
            }
            std::uint8_t* o = out.rgba.data() + (std::size_t{py} * size + px) * 4;
            if (wsum > 0) {
                o[0] = static_cast<std::uint8_t>(std::clamp(acc[0] / wsum, 0.0, 255.0));
                o[1] = static_cast<std::uint8_t>(std::clamp(acc[1] / wsum, 0.0, 255.0));
                o[2] = static_cast<std::uint8_t>(std::clamp(acc[2] / wsum, 0.0, 255.0));
            } else {
                const Rgb f = fallback ? fallback(fc, fr) : Rgb{110, 130, 90};
                o[0] = f.r, o[1] = f.g, o[2] = f.b;
            }
            o[3] = 255;
        }
    }
    return withMips(std::move(out), size);
}

const JaArchive* GroundTextures::archive(const std::filesystem::path& path) {
    auto& slot = archives_[path.string()];
    if (!slot) {
        slot = std::make_unique<JaArchive>(JaArchive::open(path));
    }
    return slot.get();
}

const SceneTexture* GroundTextures::get(const Kuid& kuid) {
    const std::string key = formatKuid(kuid);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        std::unique_ptr<SceneTexture> loaded;
        try {
            loaded = load(kuid);
        } catch (const ContentError&) {
            ++counts_["unreadable"];
        }
        it = cache_.emplace(key, std::move(loaded)).first;
    }
    return it->second.get();
}

std::unique_ptr<SceneTexture> GroundTextures::load(const Kuid& kuid) {
    auto records = index_.find(kuid);
    if (records.empty()) {
        records = index_.allVersions(kuid.user, kuid.content);
    }
    if (records.empty()) {
        ++counts_["not in the index"];
        return nullptr;
    }
    const AssetRecord& rec = *records.back();
    if (rec.where.container != AssetLocation::Container::Archive) {
        ++counts_["in a folder (not supported here yet)"];
        return nullptr;
    }
    const JaArchive* a = archive(rec.where.source);
    const std::string base = rec.where.folder.empty() ? std::string() : rec.where.folder + "/";
    const JaEntry* ce = a->find(base + rec.where.configName);
    if (ce == nullptr) {
        ++counts_["config missing"];
        return nullptr;
    }
    const ConfigNode root = parseAssetConfig(a->readLimited(*ce, std::size_t{16} << 20)).root;
    const ConfigNode* f = root.child("asset-filename");
    if (f == nullptr || f->type != ConfigType::String || f->text.empty()) {
        ++counts_["no asset-filename"];
        return nullptr;
    }
    const std::string stem = base + f->text;
    if (const JaEntry* te = a->find(stem + ".texture")) {
        const TextureFile tex = parseTexture(a->readLimited(*te, kMaxFileBytes));
        if (tex.mips.empty()) {
            ++counts_["texture without mips"];
            return nullptr;
        }
        auto out = std::make_unique<SceneTexture>();
        bool first = true;
        for (const auto& mip : tex.mips) {  // largest first
            if (mip.width > kSide || mip.height > kSide) {
                continue;
            }
            SceneTexture level;
            level.width = mip.width, level.height = mip.height;
            level.rgba = decodeToRgba8(mip, tex.info.format);
            if (first) {
                *out = std::move(level);
                first = false;
            } else {
                out->smaller.push_back(std::move(level));
            }
        }
        if (first) {  // every stored mip is larger than kSide: shrink the smallest
            SceneTexture level;
            level.width = tex.mips.back().width, level.height = tex.mips.back().height;
            level.rgba = decodeToRgba8(tex.mips.back(), tex.info.format);
            *out = withMips(std::move(level), kSide);
        }
        ++counts_["read from .texture"];
        return out;
    }
    if (const JaEntry* de = a->find(stem + ".texture.txt")) {
        const std::string descName = stem + ".texture.txt";
        const TextureDescriptor d = parseTextureDescriptor(a->readLimited(*de, std::size_t{64} << 10));
        if (d.primary.empty()) {
            ++counts_["descriptor without Primary"];
            return nullptr;
        }
        const std::string imgName = resolveDescriptorPath(descName, d.primary.front());
        const JaEntry* ie = a->find(imgName);
        if (ie == nullptr) {
            ++counts_["descriptor image missing"];
            return nullptr;
        }
        const RgbaImage img = decodeImage(imageKindFromName(imgName), a->readLimited(*ie, kMaxFileBytes));
        SceneTexture t;
        t.width = img.width, t.height = img.height, t.rgba = img.rgba;
        for (std::size_t i = 3; i < t.rgba.size(); i += 4) {
            t.rgba[i] = 255;  // ground: colour only
        }
        ++counts_["read from .texture.txt"];
        return std::make_unique<SceneTexture>(withMips(std::move(t), kSide));
    }
    ++counts_["no .texture or .texture.txt"];
    return nullptr;
}

}  // namespace trainz::view
