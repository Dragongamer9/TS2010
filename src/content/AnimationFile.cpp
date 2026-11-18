#include "AnimationFile.h"

#include "ByteOrder.h"
#include "ContentError.h"
#include "JirfFile.h"

#include <cstring>

namespace trainz::content {

namespace {

constexpr std::uint32_t kVersion = 100;
constexpr std::size_t kMaxBones = 1024;
constexpr std::uint32_t kMaxFrames = 100000;
constexpr std::uint32_t kMaxText = 4096;
constexpr std::size_t kMaxBoneDepth = 64;

class Reader {
public:
    Reader(std::span<const std::uint8_t> d, const char* what) : d_(d), what_(what) {}
    void need(std::size_t n) const {
        if (d_.size() - pos_ < n) {
            throw ContentError(std::string(what_) + ": data ends early at byte " + std::to_string(pos_));
        }
    }
    std::uint32_t u32() {
        need(4);
        const std::uint32_t v = loadU32LE(d_.data() + pos_);
        pos_ += 4;
        return v;
    }
    float f32() {
        const std::uint32_t b = u32();
        float f;
        std::memcpy(&f, &b, sizeof f);
        return f;
    }
    std::string text(std::size_t n) {
        need(n);
        const auto* p = reinterpret_cast<const char*>(d_.data() + pos_);
        const void* nul = std::memchr(p, 0, n);
        pos_ += n;
        return std::string(p, nul ? static_cast<std::size_t>(static_cast<const char*>(nul) - p) : n);
    }
    std::size_t pos() const { return pos_; }
    std::size_t left() const { return d_.size() - pos_; }

private:
    std::span<const std::uint8_t> d_;
    const char* what_;
    std::size_t pos_ = 0;
};

void expectVersion(std::uint32_t v, const char* what) {
    if (v != kVersion) {
        throw ContentError(std::string(what) + " version " + std::to_string(v) + " is unknown (100 is known)");
    }
}

// BONE: u32 100, u32 nameLen, name, u32 childCount, then childCount nested BONE chunks.
void parseBone(std::span<const std::uint8_t> data, int parent, std::size_t depth, std::vector<AnimationBone>& out) {
    if (depth > kMaxBoneDepth) {
        throw ContentError("bones nested more than " + std::to_string(kMaxBoneDepth) + " deep");
    }
    if (out.size() >= kMaxBones) {
        throw ContentError("more than " + std::to_string(kMaxBones) + " bones");
    }
    Reader r(data, "BONE");
    expectVersion(r.u32(), "BONE");
    const std::uint32_t nameLen = r.u32();
    if (nameLen > kMaxText) {
        throw ContentError("bone name too long");
    }
    AnimationBone b;
    b.name = r.text(nameLen);
    b.parent = parent;
    const std::uint32_t children = r.u32();
    const int self = static_cast<int>(out.size());
    out.push_back(std::move(b));
    const auto kids = parseJirfChunks(data.subspan(r.pos()));
    if (kids.size() != children) {
        throw ContentError("bone '" + out[static_cast<std::size_t>(self)].name + "' says " + std::to_string(children) +
                           " children but has " + std::to_string(kids.size()));
    }
    for (const auto& k : kids) {
        if (k.tag != "BONE") {
            throw ContentError("unexpected '" + k.tag + "' inside a bone");
        }
        parseBone(k.data, self, depth + 1, out);
    }
}

}  // namespace

int AnimationFile::boneIndex(const std::string& name) const {
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::array<float, 3> rotate(const std::array<float, 4>& q, const std::array<float, 3>& v) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float tx = 2 * (y * v[2] - z * v[1]);
    const float ty = 2 * (z * v[0] - x * v[2]);
    const float tz = 2 * (x * v[1] - y * v[0]);
    return {v[0] + w * tx + (y * tz - z * ty), v[1] + w * ty + (z * tx - x * tz), v[2] + w * tz + (x * ty - y * tx)};
}

AnimationFile parseAnimation(std::span<const std::uint8_t> data) {
    const JirfFile jirf = parseJirf(data);
    if (jirf.form != "ANIM") {
        throw ContentError("not an animation (JIRF form is '" + jirf.form + "', expected 'ANIM')");
    }
    const JirfChunk* info = jirf.first("INFO");
    const JirfChunk* evnt = jirf.first("EVNT");
    const JirfChunk* skel = jirf.first("SKEL");
    if (!info || !evnt || !skel) {
        throw ContentError("animation is missing its INFO, EVNT or SKEL chunk");
    }
    AnimationFile a;
    {
        Reader r(info->data, "INFO");
        expectVersion(r.u32(), "INFO");
        const std::uint32_t nameLen = r.u32();
        if (nameLen > kMaxText) {
            throw ContentError("INFO source name too long");
        }
        a.sourceFile = r.text(nameLen);
        a.frameCount = r.u32();
        a.framesPerSecond = r.u32();
        a.infoValue = r.f32();
        if (r.left() != 0 || a.frameCount > kMaxFrames) {
            throw ContentError("INFO has an unexpected size or frame count");
        }
    }
    {
        Reader r(evnt->data, "EVNT");
        expectVersion(r.u32(), "EVNT");
        const std::uint32_t n = r.u32();
        if (n > 10000) {
            throw ContentError("EVNT lists " + std::to_string(n) + " events");
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            AnimationEvent e;
            e.frame = r.u32();
            e.flags = r.u32();
            const std::uint32_t len = r.u32();
            if (len > kMaxText) {
                throw ContentError("event name too long");
            }
            e.name = r.text(len);
            a.events.push_back(std::move(e));
        }
        if (r.left() != 0) {
            throw ContentError("EVNT has " + std::to_string(r.left()) + " unexplained bytes");
        }
    }
    {
        if (skel->data.size() < 4) {
            throw ContentError("SKEL is too small");
        }
        expectVersion(loadU32LE(skel->data.data()), "SKEL");
        for (const auto& c : parseJirfChunks(skel->data.subspan(4))) {
            if (c.tag != "BONE") {
                throw ContentError("unexpected '" + c.tag + "' in SKEL");
            }
            parseBone(c.data, -1, 0, a.bones);
        }
    }
    const std::size_t frameBytes = 8 + 28 * a.bones.size();
    for (const auto& c : jirf.chunks) {
        if (c.tag != "FRAM") {
            continue;
        }
        if (c.data.size() != frameBytes) {
            throw ContentError("FRAM " + std::to_string(a.frames.size()) + " is " + std::to_string(c.data.size()) +
                               " bytes; " + std::to_string(a.bones.size()) + " bones need " + std::to_string(frameBytes));
        }
        Reader r(c.data, "FRAM");
        expectVersion(r.u32(), "FRAM");
        if (r.u32() != a.frames.size()) {
            throw ContentError("FRAM " + std::to_string(a.frames.size()) + " has the wrong frame number");
        }
        std::vector<BonePose> poses(a.bones.size());
        for (auto& p : poses) {
            p.position = {r.f32(), r.f32(), r.f32()};
            p.rotation = {r.f32(), r.f32(), r.f32(), r.f32()};
        }
        a.frames.push_back(std::move(poses));
    }
    if (a.frames.size() != a.frameCount) {
        throw ContentError("INFO says " + std::to_string(a.frameCount) + " frames but the file has " +
                           std::to_string(a.frames.size()));
    }
    return a;
}

}  // namespace trainz::content
