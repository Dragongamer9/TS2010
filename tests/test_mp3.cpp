// MP3 decoding (decodeMp3, MP3 inside a WAV, decodeSoundFile).
//
// libmpg123 cannot encode, so the tests build MPEG-1 Layer III frames by hand:
// a valid 4-byte header followed by zeroed side information and main data.
// Zero side info means no Huffman data and global gain 0, which decodes to
// 1,152 silent samples per channel per frame. Tests that need the decoder skip
// themselves in builds without libmpg123; one test checks the error then.
#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/SoundFile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace trainz::content;
using namespace trainz::test;

namespace {

Bytes chunk(const char* tag, const Bytes& body) {
    Bytes b(tag, tag + 4);
    putU32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1u) {
        b.push_back(0);
    }
    return b;
}

Bytes fmt(std::uint16_t tag, std::uint16_t ch, std::uint32_t rate, const Bytes& extra = {}) {
    Bytes f;
    putU16(f, tag);
    putU16(f, ch);
    putU32(f, rate);
    putU32(f, 4000);  // average bytes per second (not used)
    putU16(f, 1);     // block align
    putU16(f, 0);     // bits per sample
    f.insert(f.end(), extra.begin(), extra.end());
    return chunk("fmt ", f);
}

Bytes riff(const std::vector<Bytes>& chunks) {
    Bytes body = bytesOf("WAVE");
    for (const auto& c : chunks) {
        body.insert(body.end(), c.begin(), c.end());
    }
    Bytes b = bytesOf("RIFF");
    putU32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

// 32 kbit/s at 48 kHz: 144 * 32000 / 48000 = 96 bytes per frame (no padding).
// At 44.1 kHz the same bitrate gives 104 bytes (padding bit clear).
Bytes mp3Frames(int count, bool stereo = false, bool rate44100 = false) {
    Bytes frame(rate44100 ? 104 : 96, 0);
    frame[0] = 0xFF;
    frame[1] = 0xFB;                     // sync, MPEG-1, Layer III, no CRC
    frame[2] = rate44100 ? 0x10 : 0x14;  // bitrate index 1 (32k); rate index 0 (44.1k) or 1 (48k)
    frame[3] = stereo ? 0x00 : 0xC0;     // channel mode: stereo or mono
    Bytes b;
    for (int i = 0; i < count; ++i) {
        b.insert(b.end(), frame.begin(), frame.end());
    }
    return b;
}

// MPEGLAYER3WAVEFORMAT extension: cbSize 12, wID 1, flags, block size, frames per block, codec delay.
Bytes mp3WavExtra() {
    Bytes e;
    putU16(e, 12);
    putU16(e, 1);
    putU32(e, 2);
    putU16(e, 96);
    putU16(e, 1);
    putU16(e, 1393);
    return e;
}

bool allZero(const std::vector<std::int16_t>& v) {
    return std::all_of(v.begin(), v.end(), [](std::int16_t x) { return x == 0; });
}

#define REQUIRE_MP3()                                    \
    if (!mp3DecodingAvailable()) {                       \
        GTEST_SKIP() << "built without libmpg123";       \
    }

}  // namespace

TEST(Mp3, SilentFramesDecode) {
    REQUIRE_MP3();
    const auto mono = decodeMp3(mp3Frames(3));
    EXPECT_EQ(mono.channels, 1);
    EXPECT_EQ(mono.sampleRate, 48000u);
    EXPECT_EQ(mono.samples.size(), 3u * 1152u);
    EXPECT_TRUE(allZero(mono.samples));
    EXPECT_TRUE(mono.warnings.empty());

    const auto stereo = decodeMp3(mp3Frames(4, true, true));
    EXPECT_EQ(stereo.channels, 2);
    EXPECT_EQ(stereo.sampleRate, 44100u);
    EXPECT_EQ(stereo.samples.size(), 4u * 1152u * 2u);
    EXPECT_TRUE(allZero(stereo.samples));
}

TEST(Mp3, Id3TagIsSkippedAndNamesPickTheDecoder) {
    REQUIRE_MP3();
    // ID3v2.3 header with a 20-byte (syncsafe) body; the disc's .mp3 files start with one.
    Bytes b = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 20};
    b.resize(b.size() + 20, 0);
    const Bytes frames = mp3Frames(3);
    b.insert(b.end(), frames.begin(), frames.end());
    EXPECT_EQ(decodeMp3(b).samples.size(), 3u * 1152u);
    EXPECT_EQ(decodeSoundFile("Libraries/CommandSounds/X.MP3", b).samples.size(), 3u * 1152u);
    // A bare MP3 stream with a .wav name is decoded too, with a warning.
    const auto asWav = decodeSoundFile("sound.wav", b);
    EXPECT_EQ(asWav.samples.size(), 3u * 1152u);
    ASSERT_FALSE(asWav.warnings.empty());
    EXPECT_NE(asWav.warnings[0].find("really an MP3"), std::string::npos);
}

TEST(Mp3, InsideAWav) {
    REQUIRE_MP3();
    const auto s =
        decodeWav(riff({fmt(85, 1, 48000, mp3WavExtra()), chunk("fact", Bytes(4, 0)), chunk("data", mp3Frames(5))}));
    EXPECT_EQ(s.channels, 1);
    EXPECT_EQ(s.sampleRate, 48000u);
    EXPECT_EQ(s.samples.size(), 5u * 1152u);
    EXPECT_TRUE(s.warnings.empty());
    // When the WAV header disagrees with the stream, the stream wins and a warning says so.
    const auto odd = decodeWav(riff({fmt(85, 2, 22050, mp3WavExtra()), chunk("data", mp3Frames(5))}));
    EXPECT_EQ(odd.channels, 1);
    EXPECT_EQ(odd.sampleRate, 48000u);
    EXPECT_FALSE(odd.warnings.empty());
}

TEST(Mp3, PartialLastFrameIsDroppedWithAWarning) {
    REQUIRE_MP3();
    Bytes b = mp3Frames(3);
    b.resize(b.size() - 40);
    const auto s = decodeMp3(b);
    EXPECT_EQ(s.samples.size(), 2u * 1152u);
    EXPECT_FALSE(s.warnings.empty());
}

TEST(Mp3, GarbageAndDamageThrow) {
    REQUIRE_MP3();
    EXPECT_THROW(decodeMp3(Bytes{}), ContentError);
    EXPECT_THROW(decodeMp3(Bytes(4096, 0)), ContentError);
    EXPECT_THROW(decodeMp3(Bytes(4096, 0xFF)), ContentError);
    EXPECT_THROW(decodeMp3(bytesOf("this is not an mp3 file, just some text that goes on for a while")), ContentError);
    Bytes cut = mp3Frames(1);
    cut.resize(50);  // less than one frame
    EXPECT_THROW(decodeMp3(cut), ContentError);
    // MP3 inside a WAV whose data is not MPEG audio.
    EXPECT_THROW(decodeWav(riff({fmt(85, 1, 24000), chunk("data", Bytes(10, 0))})), ContentError);
    // The sample rate changes part-way.
    Bytes mixed = mp3Frames(4);
    const Bytes other = mp3Frames(4, false, true);
    mixed.insert(mixed.end(), other.begin(), other.end());
    EXPECT_THROW(decodeMp3(mixed), ContentError);
}

TEST(Mp3, DecodedSizeIsCapped) {
    REQUIRE_MP3();
    const Bytes b = mp3Frames(10);
    EXPECT_EQ(decodeMp3(b, 10u * 1152u * 2u).samples.size(), 10u * 1152u);
    EXPECT_THROW(decodeMp3(b, 10u * 1152u * 2u - 2u), ContentError);
}

TEST(Mp3, EveryTruncationAndByteChangeIsSafe) {
    REQUIRE_MP3();
    Bytes good = mp3Frames(3, true);
    for (std::size_t i = 4; i < good.size(); i += 7) {
        good[i] = static_cast<std::uint8_t>(i * 37u);  // non-zero side info and data, so damage reaches the decoder
    }
    auto tryAll = [](const Bytes& b) {
        try {
            (void)decodeMp3(b);
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
    std::uint32_t x = 12345;  // pseudo-random bytes, fixed seed
    for (int round = 0; round < 50; ++round) {
        Bytes b(2000);
        for (auto& c : b) {
            x = x * 1664525u + 1013904223u;
            c = static_cast<std::uint8_t>(x >> 24);
        }
        tryAll(b);
    }
}

TEST(Mp3, WithoutTheDecoderTheErrorIsClear) {
    if (mp3DecodingAvailable()) {
        GTEST_SKIP() << "built with libmpg123";
    }
    try {
        (void)decodeMp3(mp3Frames(3));
        FAIL() << "expected a ContentError";
    } catch (const ContentError& e) {
        EXPECT_NE(std::string(e.what()).find("not built in"), std::string::npos);
    }
    try {
        (void)decodeWav(riff({fmt(85, 1, 48000), chunk("data", mp3Frames(3))}));
        FAIL() << "expected a ContentError";
    } catch (const ContentError& e) {
        EXPECT_NE(std::string(e.what()).find("MP3 inside"), std::string::npos);
    }
}
