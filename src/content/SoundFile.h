// .wav sounds (RIFF/WAVE). See docs/formats/sound.md.
//
// Standard formats, but the disc's files are messy; the rules here come from
// checking all 5,874 .wav files in Base.ja, Data.ja and Builtin\1.ja-8.ja:
//   - PCM (tag 1), 8-bit unsigned or 16-bit signed, mono or stereo: 5,844 files
//   - Microsoft ADPCM (tag 2, 4-bit): 26 files, 9 of them ending in a partial block
//   - MP3 inside a WAV (tag 85): 3 files (not decoded yet)
//   - many extra chunks (LIST, smpl, cue, bext, ...), sometimes garbage after
//     the data, a RIFF size one byte past the end of the file (2 files), or
//     extra bytes after the RIFF data (67 files): all tolerated with warnings
//   - one ".wav" is really an MPEG stream (rejected with a clear error)
#pragma once

#include <cstdint>
#include <span>
#include <string>
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
// handled (MP3 inside WAV, other tags, PCM other than 8/16-bit).
PcmSound decodeWav(std::span<const std::uint8_t> data);

// A plain 16-bit PCM .wav file (for tools: exporting what was decoded).
std::vector<std::uint8_t> encodePcmWav(const PcmSound& sound);

}  // namespace trainz::content
