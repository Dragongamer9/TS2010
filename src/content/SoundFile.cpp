#include "SoundFile.h"

#include "ByteOrder.h"
#include "ContentError.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace trainz::content {

namespace {

constexpr std::uint16_t kTagPcm = 1;
constexpr std::uint16_t kTagMsAdpcm = 2;
constexpr std::uint16_t kTagMp3 = 85;

std::int16_t loadS16LE(const std::uint8_t* p) { return static_cast<std::int16_t>(loadU16LE(p)); }


// Microsoft ADPCM (the standard algorithm, as in the Windows msadpcm codec).
const std::array<int, 16> kAdaptation = {230, 230, 230, 230, 307, 409, 512, 614,
                                         768, 614, 512, 409, 307, 230, 230, 230};

void decodeMsAdpcm(const WavInfo& w, std::span<const std::uint8_t> d, PcmSound& out) {
    const unsigned ch = w.channels;
    if (ch == 0 || ch > 2) {
        throw ContentError("MS ADPCM with " + std::to_string(ch) + " channels is not supported");
    }
    if (w.formatExtra.size() < 6) {
        throw ContentError("MS ADPCM format block is too short");
    }
    const std::uint16_t samplesPerBlock = loadU16LE(w.formatExtra.data() + 2);
    const std::uint16_t numCoef = loadU16LE(w.formatExtra.data() + 4);
    if (numCoef == 0 || numCoef > 256 || w.formatExtra.size() < 6 + 4u * numCoef) {
        throw ContentError("MS ADPCM coefficient table is damaged");
    }
    std::vector<std::array<int, 2>> coef(numCoef);
    for (unsigned i = 0; i < numCoef; ++i) {
        coef[i] = {loadS16LE(w.formatExtra.data() + 6 + 4 * i), loadS16LE(w.formatExtra.data() + 8 + 4 * i)};
    }
    const std::size_t block = w.blockAlign;
    if (block < 7 * ch || samplesPerBlock < 2) {
        throw ContentError("MS ADPCM block size does not fit its header");
    }
    for (std::size_t off = 0; off < d.size(); off += block) {
        const std::size_t len = std::min(block, d.size() - off);
        if (len < 7 * ch) {
            out.warnings.push_back("last ADPCM block is too short to decode and was dropped");
            break;
        }
        const std::uint8_t* b = d.data() + off;
        int pred[2] = {0, 0}, delta[2] = {0, 0}, s1[2] = {0, 0}, s2[2] = {0, 0};
        for (unsigned c = 0; c < ch; ++c) {
            pred[c] = b[c];
            if (pred[c] >= numCoef) {
                throw ContentError("MS ADPCM block uses coefficient " + std::to_string(pred[c]) + " of " +
                                   std::to_string(numCoef));
            }
            delta[c] = loadS16LE(b + ch + 2 * c);
            s1[c] = loadS16LE(b + 3 * ch + 2 * c);
            s2[c] = loadS16LE(b + 5 * ch + 2 * c);
        }
        // The header holds the first two samples: sample2 comes first, then sample1.
        for (unsigned c = 0; c < ch; ++c) {
            out.samples.push_back(static_cast<std::int16_t>(s2[c]));
        }
        for (unsigned c = 0; c < ch; ++c) {
            out.samples.push_back(static_cast<std::int16_t>(s1[c]));
        }
        std::size_t produced = 2;
        unsigned c = 0;
        for (std::size_t i = 7 * ch; i < len && produced < samplesPerBlock; ++i) {
            for (int half = 0; half < 2 && produced < samplesPerBlock; ++half) {
                const int nib = half == 0 ? (b[i] >> 4) : (b[i] & 0x0F);
                const int signedNib = nib >= 8 ? nib - 16 : nib;
                const int predicted = (s1[c] * coef[static_cast<std::size_t>(pred[c])][0] +
                                       s2[c] * coef[static_cast<std::size_t>(pred[c])][1]) / 256;
                const int sample = std::clamp(predicted + signedNib * delta[c], -32768, 32767);
                out.samples.push_back(static_cast<std::int16_t>(sample));
                s2[c] = s1[c];
                s1[c] = sample;
                delta[c] = std::max(16, (kAdaptation[static_cast<std::size_t>(nib)] * delta[c]) / 256);
                if (++c == ch) {
                    c = 0;
                    ++produced;
                }
            }
        }
        if (len < block) {
            out.warnings.push_back("last ADPCM block is partial (" + std::to_string(len) + " of " +
                                   std::to_string(block) + " bytes); decoded what is there");
        }
    }
}

}  // namespace

WavInfo parseWav(std::span<const std::uint8_t> data) {
    if (data.size() >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 1 && data[3] == 0xBA) {
        throw ContentError("this is an MPEG stream, not a WAV file (it has a .wav name)");
    }
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        throw ContentError("not a RIFF/WAVE file");
    }
    WavInfo w;
    std::uint64_t riffEnd = static_cast<std::uint64_t>(loadU32LE(data.data() + 4)) + 8;
    if (riffEnd > data.size()) {
        w.warnings.push_back("RIFF size runs " + std::to_string(riffEnd - data.size()) + " bytes past the end of the file");
        riffEnd = data.size();
    } else if (riffEnd < data.size()) {
        w.warnings.push_back(std::to_string(data.size() - riffEnd) + " bytes after the RIFF data are ignored");
    }
    bool haveFmt = false;
    bool haveData = false;
    std::size_t pos = 12;
    while (pos + 8 <= riffEnd) {
        const std::uint8_t* c = data.data() + pos;
        const std::uint32_t size = loadU32LE(c + 4);
        const std::uint64_t bodyEnd = static_cast<std::uint64_t>(pos) + 8 + size;
        const std::string tag(reinterpret_cast<const char*>(c), 4);
        if (std::memcmp(c, "data", 4) == 0 && !haveData) {
            haveData = true;
            w.dataOffset = pos + 8;
            w.dataSize = static_cast<std::size_t>(std::min<std::uint64_t>(size, riffEnd - (pos + 8)));
            if (w.dataSize < size) {
                w.warnings.push_back("data chunk runs past the end of the file; using the " +
                                     std::to_string(w.dataSize) + " bytes that are there");
            }
        } else if (bodyEnd > riffEnd) {
            if (haveFmt && haveData) {
                w.warnings.push_back("damaged chunk '" + tag + "' after the sound data ignored");
                break;
            }
            throw ContentError("chunk '" + tag + "' runs past the end of the file");
        } else if (std::memcmp(c, "fmt ", 4) == 0 && !haveFmt) {
            if (size < 16) {
                throw ContentError("fmt chunk is only " + std::to_string(size) + " bytes");
            }
            haveFmt = true;
            w.formatTag = loadU16LE(c + 8);
            w.channels = loadU16LE(c + 10);
            w.sampleRate = loadU32LE(c + 12);
            w.blockAlign = loadU16LE(c + 20);
            w.bitsPerSample = loadU16LE(c + 22);
            w.formatExtra.assign(c + 24, c + 8 + size);
        } else {
            w.otherChunks.push_back(tag);
        }
        if (bodyEnd > riffEnd) {
            break;
        }
        pos = static_cast<std::size_t>(bodyEnd + (size & 1u));  // chunks are padded to even sizes
    }
    if (!haveFmt || !haveData) {
        throw ContentError(std::string("WAV file has no ") + (haveFmt ? "data" : "fmt") + " chunk");
    }
    if (w.channels == 0 || w.channels > 8 || w.sampleRate == 0 || w.sampleRate > 384000) {
        throw ContentError("WAV format values are not usable (" + std::to_string(w.channels) + " channels, " +
                           std::to_string(w.sampleRate) + " Hz)");
    }
    return w;
}

PcmSound decodeWav(std::span<const std::uint8_t> data) {
    const WavInfo w = parseWav(data);
    PcmSound out;
    out.channels = w.channels;
    out.sampleRate = w.sampleRate;
    out.warnings = w.warnings;
    const auto d = data.subspan(w.dataOffset, w.dataSize);
    switch (w.formatTag) {
        case kTagPcm: {
            if (w.bitsPerSample == 8) {
                const std::size_t n = d.size() - d.size() % w.channels;
                out.samples.reserve(n);
                for (std::size_t i = 0; i < n; ++i) {
                    out.samples.push_back(static_cast<std::int16_t>((static_cast<int>(d[i]) - 128) * 256));
                }
            } else if (w.bitsPerSample == 16) {
                const std::size_t frame = 2u * w.channels;
                const std::size_t n = (d.size() / frame) * w.channels;
                out.samples.reserve(n);
                for (std::size_t i = 0; i < n; ++i) {
                    out.samples.push_back(loadS16LE(d.data() + 2 * i));
                }
            } else {
                throw ContentError("PCM with " + std::to_string(w.bitsPerSample) + " bits per sample is not supported");
            }
            if (out.samples.size() * (w.bitsPerSample / 8u) != d.size()) {
                out.warnings.push_back("data ends in the middle of a sample frame; the last partial frame was dropped");
            }
            return out;
        }
        case kTagMsAdpcm:
            decodeMsAdpcm(w, d, out);
            return out;
        case kTagMp3:
            throw ContentError("MP3 inside a WAV file is not decoded yet (3 files on the disc)");
        default:
            throw ContentError("WAV format tag " + std::to_string(w.formatTag) + " is not supported");
    }
}

std::vector<std::uint8_t> encodePcmWav(const PcmSound& s) {
    std::vector<std::uint8_t> b;
    auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    };
    auto u16 = [&](std::uint16_t v) {
        b.push_back(static_cast<std::uint8_t>(v));
        b.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(s.samples.size() * 2);
    b.insert(b.end(), {'R', 'I', 'F', 'F'});
    u32(36 + dataBytes);
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    u32(16);
    u16(1);
    u16(s.channels);
    u32(s.sampleRate);
    u32(s.sampleRate * 2u * s.channels);
    u16(static_cast<std::uint16_t>(2 * s.channels));
    u16(16);
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    u32(dataBytes);
    for (std::int16_t v : s.samples) {
        u16(static_cast<std::uint16_t>(v));
    }
    return b;
}

}  // namespace trainz::content
