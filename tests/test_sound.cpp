#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/SoundFile.h"

#include <gtest/gtest.h>

using namespace trainz::content;
using namespace trainz::test;

namespace {

Bytes chunk(const char* tag, const Bytes& body) {
    Bytes b(tag, tag + 4);
    putU32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1u) {
        b.push_back(0);  // RIFF chunks are padded to even sizes
    }
    return b;
}

Bytes fmt(std::uint16_t tag, std::uint16_t ch, std::uint32_t rate, std::uint16_t bits, std::uint16_t align,
          const Bytes& extra = {}) {
    Bytes f;
    putU16(f, tag);
    putU16(f, ch);
    putU32(f, rate);
    putU32(f, rate * align);
    putU16(f, align);
    putU16(f, bits);
    f.insert(f.end(), extra.begin(), extra.end());
    return chunk("fmt ", f);
}

Bytes riff(const std::vector<Bytes>& chunks, int riffSizeAdjust = 0) {
    Bytes body = bytesOf("WAVE");
    for (const auto& c : chunks) {
        body.insert(body.end(), c.begin(), c.end());
    }
    Bytes b = bytesOf("RIFF");
    putU32(b, static_cast<std::uint32_t>(static_cast<int>(body.size()) + riffSizeAdjust));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

// MS ADPCM fmt extension: cbSize, samplesPerBlock, numCoef, then the 7 standard coefficient pairs.
Bytes adpcmExtra(std::uint16_t samplesPerBlock) {
    Bytes e;
    putU16(e, 32);
    putU16(e, samplesPerBlock);
    putU16(e, 7);
    const int coef[7][2] = {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};
    for (const auto& c : coef) {
        putU16(e, static_cast<std::uint16_t>(c[0]));
        putU16(e, static_cast<std::uint16_t>(c[1]));
    }
    return e;
}

}  // namespace

TEST(Wav, Pcm16Mono) {
    Bytes d;
    for (std::int16_t v : {0, 1000, -1000, 32767}) {
        putU16(d, static_cast<std::uint16_t>(v));
    }
    const auto s = decodeWav(riff({fmt(1, 1, 22050, 16, 2), chunk("data", d)}));
    EXPECT_EQ(s.channels, 1);
    EXPECT_EQ(s.sampleRate, 22050u);
    EXPECT_EQ(s.samples, (std::vector<std::int16_t>{0, 1000, -1000, 32767}));
    EXPECT_TRUE(s.warnings.empty());
}

TEST(Wav, Pcm8IsUnsigned) {
    const auto s = decodeWav(riff({fmt(1, 1, 8000, 8, 1), chunk("data", Bytes{128, 0, 255})}));
    EXPECT_EQ(s.samples, (std::vector<std::int16_t>{0, -32768, 32512}));
}

TEST(Wav, StereoAndExtraChunks) {
    Bytes d;
    for (std::int16_t v : {1, 2, 3, 4}) {
        putU16(d, static_cast<std::uint16_t>(v));
    }
    // bext before fmt, odd-sized LIST (padded), cue after data: all seen on the disc
    const auto w = parseWav(riff({chunk("bext", Bytes(5, 7)), fmt(1, 2, 44100, 16, 4), chunk("LIST", Bytes(3, 1)),
                                  chunk("data", d), chunk("cue ", Bytes(4, 0))}));
    EXPECT_EQ(w.channels, 2);
    EXPECT_EQ(w.dataSize, 8u);
    EXPECT_EQ(w.otherChunks, (std::vector<std::string>{"bext", "LIST", "cue "}));
    EXPECT_EQ(decodeWav(riff({fmt(1, 2, 44100, 16, 4), chunk("data", d)})).samples.size(), 4u);
}

TEST(Wav, DamageAfterTheDataIsTolerated) {
    Bytes d(4, 0);
    Bytes b = riff({fmt(1, 1, 8000, 16, 2), chunk("data", d)});
    b.insert(b.end(), {'J', 'U', 'N', 'K', 0xFF, 0xFF, 0xFF, 0x7F});  // garbage chunk claiming 2 GB
    setU32(b, 4, static_cast<std::uint32_t>(b.size() - 8));
    const auto w = parseWav(b);
    EXPECT_EQ(w.dataSize, 4u);
    EXPECT_FALSE(w.warnings.empty());
}

TEST(Wav, RiffSizeOnePastTheEnd) {
    const auto s = decodeWav(riff({fmt(1, 1, 8000, 16, 2), chunk("data", Bytes(4, 0))}, 1));
    EXPECT_EQ(s.samples.size(), 2u);
    EXPECT_FALSE(s.warnings.empty());
}

TEST(Wav, DataChunkLongerThanTheFileIsClamped) {
    Bytes b = riff({fmt(1, 1, 8000, 16, 2), chunk("data", Bytes(6, 0))});
    setU32(b, b.size() - 6 - 4, 1000);  // data size claims 1000 bytes
    const auto s = decodeWav(b);
    EXPECT_EQ(s.samples.size(), 3u);
    EXPECT_FALSE(s.warnings.empty());
}

TEST(Wav, MsAdpcmHandComputed) {
    // One mono block: predictor 0 (256, 0), delta 16, sample1 100, sample2 50, nibbles 0x1F.
    // Output: 50, 100, then 100 + 1*16 = 116, then 116 + (-1)*16 = 100.
    Bytes block = {0x00};
    putU16(block, 16);
    putU16(block, 100);
    putU16(block, 50);
    block.push_back(0x1F);
    const auto s = decodeWav(riff({fmt(2, 1, 11025, 4, 8, adpcmExtra(4)), chunk("data", block)}));
    EXPECT_EQ(s.samples, (std::vector<std::int16_t>{50, 100, 116, 100}));
}

// Regression: with extreme coefficients from the file, s1*c0 + s2*c1 needs more than
// 32 bits (2 * 32768 * 32768 = 2^31). It must clamp, not overflow.
TEST(Wav, MsAdpcmExtremeCoefficientsDoNotOverflow) {
    Bytes extra;
    putU16(extra, 8);
    putU16(extra, 4);  // samples per block
    putU16(extra, 1);  // one coefficient pair
    putU16(extra, 0x8000);
    putU16(extra, 0x8000);  // (-32768, -32768)
    Bytes block = {0x00};
    putU16(block, 16);      // delta
    putU16(block, 0x8000);  // sample1 -32768
    putU16(block, 0x8000);  // sample2 -32768
    block.push_back(0x00);  // two zero nibbles
    const auto s = decodeWav(riff({fmt(2, 1, 11025, 4, 8, extra), chunk("data", block)}));
    // 2^31 / 256 clamps to 32767; then (32767*-32768 + -32768*-32768) / 256 = 128.
    EXPECT_EQ(s.samples, (std::vector<std::int16_t>{-32768, -32768, 32767, 128}));
}

// Regression: the step size grows by up to 3x per nibble (adaptation 768/256). Without
// a cap, a run of loud nibbles overflows int within a handful of samples.
TEST(Wav, MsAdpcmStepSizeIsCapped) {
    constexpr std::uint16_t kSamples = 2 + 2 * 40;
    Bytes block = {0x00};
    putU16(block, 32767);  // the largest starting delta
    putU16(block, 0);
    putU16(block, 0);
    for (int i = 0; i < 40; ++i) {
        block.push_back(0x88);  // nibble 8 (-8, adaptation 768) twice per byte
    }
    const auto s = decodeWav(riff({fmt(2, 1, 11025, 4, static_cast<std::uint16_t>(block.size()),
                                       adpcmExtra(kSamples)),
                                   chunk("data", block)}));
    ASSERT_EQ(s.samples.size(), kSamples);
    EXPECT_EQ(s.samples.back(), -32768);  // pinned at the bottom, no wrap-around
}

TEST(Wav, MsAdpcmPartialLastBlock) {
    Bytes data;
    for (int k = 0; k < 2; ++k) {
        data.push_back(0x00);
        putU16(data, 16);
        putU16(data, 100);
        putU16(data, 50);
        data.push_back(0x1F);
    }
    data.resize(data.size() - 1);  // second block loses its nibble byte
    const auto s = decodeWav(riff({fmt(2, 1, 11025, 4, 8, adpcmExtra(4)), chunk("data", data)}));
    EXPECT_EQ(s.samples.size(), 4u + 2u);
    EXPECT_FALSE(s.warnings.empty());
}

TEST(Wav, UnsupportedAndWrongFilesGiveClearErrors) {
    EXPECT_THROW(decodeWav(riff({fmt(85, 1, 24000, 0, 1), chunk("data", Bytes(10, 0))})), ContentError);
    EXPECT_THROW(decodeWav(riff({fmt(1, 1, 8000, 24, 3), chunk("data", Bytes(6, 0))})), ContentError);
    EXPECT_THROW(parseWav(Bytes{0x00, 0x00, 0x01, 0xBA, 0x44, 0x00}), ContentError);  // MPEG stream
    EXPECT_THROW(parseWav(bytesOf("RIFF")), ContentError);
    EXPECT_THROW(parseWav(riff({fmt(1, 1, 8000, 16, 2)})), ContentError);  // no data
    EXPECT_THROW(parseWav(riff({fmt(1, 0, 8000, 16, 2), chunk("data", Bytes(2, 0))})), ContentError);
    Bytes badCoef = {0x09};  // predictor 9 of 7
    putU16(badCoef, 16), putU16(badCoef, 0), putU16(badCoef, 0);
    badCoef.push_back(0);
    EXPECT_THROW(decodeWav(riff({fmt(2, 1, 11025, 4, 8, adpcmExtra(4)), chunk("data", badCoef)})), ContentError);
}

TEST(Wav, EveryTruncationAndByteChangeIsSafe) {
    Bytes block = {0x01};
    putU16(block, 20), putU16(block, 5), putU16(block, -5);
    block.insert(block.end(), {0x12, 0x34, 0x56});
    const Bytes good = riff({chunk("bext", Bytes(3, 1)), fmt(2, 1, 11025, 4, 10, adpcmExtra(8)), chunk("data", block)});
    auto tryAll = [](const Bytes& b) {
        try {
            (void)decodeWav(b);
        } catch (const ContentError&) {
        }
    };
    for (std::size_t len = 0; len <= good.size(); ++len) {
        tryAll(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len)));
    }
    for (std::size_t i = 0; i < good.size(); ++i) {
        for (std::uint8_t v : {std::uint8_t{0}, std::uint8_t{0xFF}, std::uint8_t{0x81}}) {
            Bytes b = good;
            b[i] = v;
            tryAll(b);
        }
    }
}

TEST(Wav, ExportRoundTrips) {
    PcmSound s;
    s.channels = 2;
    s.sampleRate = 22050;
    s.samples = {1, -2, 3, -4};
    const auto back = decodeWav(encodePcmWav(s));
    EXPECT_EQ(back.samples, s.samples);
    EXPECT_EQ(back.channels, 2);
    EXPECT_EQ(back.sampleRate, 22050u);
}
