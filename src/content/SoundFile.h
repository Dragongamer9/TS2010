// .wav sounds (RIFF/WAVE). See docs/formats/sound.md.
//
// Standard formats, but the disc's files are messy; the rules here come from
// checking all 5,874 .wav files in Base.ja, Data.ja and Builtin\1.ja-8.ja:
//   - PCM (tag 1), 8-bit unsigned or 16-bit signed, mono or stereo: 5,844 files
//   - Microsoft ADPCM (tag 2, 4-bit): 26 files, 9 of them ending in a partial block
//   - MP3 inside a WAV (tag 85): 3 files, decoded with libmpg123 when built with it
//   - many extra chunks (LIST, smpl, cue, bext, ...), sometimes garbage after
//     the data, a RIFF size one byte past the end of the file (2 files), or
//     extra bytes after the RIFF data (67 files): all tolerated with warnings
//   - one ".wav" is really an MPEG-2 program stream (DVD-style video with AC-3
//     audio, no MPEG audio): rejected with a clear error
// Plus three real .mp3 files (Base.ja), decoded with libmpg123.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trainz::content {

struct WavInfo {
    std::uint16_t formatTag = 0;  // 1 PCM, 2 MS ADPCM, 85 MP3
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t blockAlign = 0;
    std::uint16_t bitsPerSample = 0;
    std::size_t dataOffset = 0;  // byte offset of the sample data in the file
    std::size_t dataSize = 0;    // clamped to the file
    std::vector<std::uint8_t> formatExtra;  // fmt bytes after the first 16 (ADPCM coefficients etc.)
    std::vector<std::string> otherChunks;
    std::vector<std::string> warnings;
};

struct PcmSound {
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::vector<std::int16_t> samples;  // interleaved, 16-bit signed
    std::vector<std::string> warnings;
};

// Throws ContentError if it is not a usable RIFF/WAVE file.
WavInfo parseWav(std::span<const std::uint8_t> data);

// Decodes to 16-bit PCM. Throws ContentError for damage and for formats not
// handled (other tags, PCM other than 8/16-bit, MPEG program streams). MP3
// inside a WAV (tag 85), and a bare MP3 stream with a .wav name, go to
// decodeMp3.
PcmSound decodeWav(std::span<const std::uint8_t> data);

// True when this build has the MP3 decoder (libmpg123). Without it, decodeMp3
// throws a ContentError that says so.
bool mp3DecodingAvailable();

// Upper bound on a decoded MP3 (16-bit samples), checked before allocating.
inline constexpr std::size_t kMaxDecodedSoundBytes = std::size_t{256} << 20;

// Decodes an MPEG audio stream (MPEG-1/2/2.5 Layer I/II/III; ID3 tags are
// skipped) to 16-bit PCM. Throws ContentError if no audio frame is found, the
// stream changes rate or channel count part-way, or the decoded sound would
// exceed maxDecodedBytes. A partial frame at the end is dropped with a warning.
PcmSound decodeMp3(std::span<const std::uint8_t> data, std::size_t maxDecodedBytes = kMaxDecodedSoundBytes);

// Picks decodeMp3 for names ending in ".mp3" (any case) and decodeWav otherwise.
PcmSound decodeSoundFile(std::string_view name, std::span<const std::uint8_t> data);

// A plain 16-bit PCM .wav file (for tools: exporting what was decoded).
std::vector<std::uint8_t> encodePcmWav(const PcmSound& sound);

}  // namespace trainz::content
