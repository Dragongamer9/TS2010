// MPEG audio (MP3) decoding through libmpg123, fed from memory. See
// docs/formats/sound.md.
//
// Built with TRAINZ_HAVE_MPG123 when CMake finds libmpg123 (Debian
// libmpg123-dev); without it, decodeMp3 throws a ContentError that says so.
#include "SoundFile.h"

#include "ContentError.h"

#include <cctype>
#include <cstring>

#if TRAINZ_HAVE_MPG123
// mpg123.h is a C header: a function and a struct share the name mpg123_fmt.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <mpg123.h>
#pragma GCC diagnostic pop

#include <array>
#include <memory>
#endif

namespace trainz::content {

#if TRAINZ_HAVE_MPG123

namespace {

struct HandleDeleter {
    void operator()(mpg123_handle* h) const { mpg123_delete(h); }
};

[[noreturn]] void fail(const std::string& what) { throw ContentError("MP3 decoding failed: " + what); }

}  // namespace

bool mp3DecodingAvailable() { return true; }

PcmSound decodeMp3(std::span<const std::uint8_t> data, std::size_t maxDecodedBytes) {
    if (data.empty()) {
        fail("the stream is empty");
    }
    // A no-op since mpg123 1.27, still required by older versions; runs once.
    static const int initResult = mpg123_init();
    if (initResult != MPG123_OK) {
        fail(std::string("cannot initialise libmpg123: ") + mpg123_plain_strerror(initResult));
    }
    int err = MPG123_OK;
    std::unique_ptr<mpg123_handle, HandleDeleter> h(mpg123_new(nullptr, &err));
    if (!h) {
        fail(std::string("cannot create a decoder: ") + mpg123_plain_strerror(err));
    }
    // No messages on stderr: problems come back as return codes.
    mpg123_param(h.get(), MPG123_ADD_FLAGS, MPG123_QUIET, 0.0);
    // Output 16-bit signed native-endian at the stream's own rate and channel count.
    mpg123_format_none(h.get());
    const long* rates = nullptr;
    std::size_t rateCount = 0;
    mpg123_rates(&rates, &rateCount);
    for (std::size_t i = 0; i < rateCount; ++i) {
        mpg123_format(h.get(), rates[i], MPG123_MONO | MPG123_STEREO, MPG123_ENC_SIGNED_16);
    }
    if (mpg123_open_feed(h.get()) != MPG123_OK) {
        fail(mpg123_strerror(h.get()));
    }
    // The whole stream is already in memory: hand it over in one piece.
    if (mpg123_feed(h.get(), data.data(), data.size()) != MPG123_OK) {
        fail(mpg123_strerror(h.get()));
    }

    PcmSound out;
    bool haveFormat = false;
    const std::size_t maxSamples = maxDecodedBytes / sizeof(std::int16_t);
    std::array<unsigned char, 32768> buf{};
    for (;;) {
        std::size_t done = 0;
        const int r = mpg123_read(h.get(), buf.data(), buf.size(), &done);
        if (r == MPG123_NEW_FORMAT) {
            long rate = 0;
            int channels = 0;
            int encoding = 0;
            if (mpg123_getformat(h.get(), &rate, &channels, &encoding) != MPG123_OK) {
                fail(mpg123_strerror(h.get()));
            }
            if (encoding != MPG123_ENC_SIGNED_16 || channels < 1 || channels > 2 || rate <= 0 || rate > 384000) {
                fail("unexpected output format (" + std::to_string(channels) + " channels, " + std::to_string(rate) +
                     " Hz)");
            }
            if (haveFormat && (static_cast<std::uint32_t>(rate) != out.sampleRate || channels != out.channels)) {
                throw ContentError("MP3 stream changes from " + std::to_string(out.sampleRate) + " Hz / " +
                                   std::to_string(out.channels) + " channel(s) to " + std::to_string(rate) +
                                   " Hz / " + std::to_string(channels) + " part-way; not supported");
            }
            haveFormat = true;
            out.sampleRate = static_cast<std::uint32_t>(rate);
            out.channels = static_cast<std::uint16_t>(channels);
        }
        if (done > 0) {
            if (!haveFormat || done > buf.size()) {
                fail("decoder returned samples before a format");
            }
            const std::size_t n = done / sizeof(std::int16_t);
            if (n > maxSamples - out.samples.size()) {
                throw ContentError("decoded MP3 would be larger than " + std::to_string(maxDecodedBytes) +
                                   " bytes; refused");
            }
            const std::size_t old = out.samples.size();
            out.samples.resize(old + n);
            for (std::size_t i = 0; i < n; ++i) {
                // mpg123 writes native-endian 16-bit samples.
                std::int16_t v = 0;
                std::memcpy(&v, buf.data() + 2 * i, sizeof v);
                out.samples[old + i] = v;
            }
        }
        if (r == MPG123_OK || r == MPG123_NEW_FORMAT) {
            continue;
        }
        if (r == MPG123_NEED_MORE || r == MPG123_DONE) {
            break;  // the whole buffer has been used
        }
        fail(mpg123_strerror(h.get()));
    }
    if (!haveFormat || out.samples.empty()) {
        throw ContentError("no MPEG audio frames found (not an MP3 stream, or too damaged to decode)");
    }
    long left = 0;
    double unused = 0;
    if (mpg123_getstate(h.get(), MPG123_BUFFERFILL, &left, &unused) == MPG123_OK && left > 0) {
        out.warnings.push_back(std::to_string(left) + " bytes at the end are not a complete MPEG frame; ignored");
    }
    if (out.samples.size() % out.channels != 0) {
        out.samples.resize(out.samples.size() - out.samples.size() % out.channels);
    }
    return out;
}

#else  // !TRAINZ_HAVE_MPG123

bool mp3DecodingAvailable() { return false; }

PcmSound decodeMp3(std::span<const std::uint8_t>, std::size_t) {
    throw ContentError("MP3 decoding is not built in (install libmpg123-dev and rebuild)");
}

#endif

PcmSound decodeSoundFile(std::string_view name, std::span<const std::uint8_t> data) {
    if (name.size() >= 4) {
        std::string ext(name.substr(name.size() - 4));
        for (char& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext == ".mp3") {
            return decodeMp3(data);
        }
    }
    return decodeWav(data);
}

}  // namespace trainz::content
