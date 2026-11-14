// A train's own sounds from its files, mixed to mono for playback: the traincar's
// `enginesound` asset (OBSERVED layout on the disc, e.g. <kuid:-1:42003000>: one loop per notch
// "idle 1.wav" .. "idle 8.wav", a clip for every notch change "up 3 - 4.wav" / "down 4 - 3.wav",
// and "stop.wav") and its `hornsound` asset (with "two-part 1": "horn_start.wav", "horn_loop.wav",
// "horn_end.wav"). How the game picks and blends them (its soundscript, volumes, pitch with
// speed) is not traced; the port's rules: notch 0 plays "idle 1", a notch change plays the
// clips step by step and then the new loop, the horn starts, loops while held and ends.
#pragma once

#include "content/Kuid.h"
#include "content/SoundFile.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace trainz::game {

// A clip as mono float samples (-1..1) at the mixer's rate.
struct SoundClip {
    std::vector<float> samples;
};
// Mixes down and resamples (linearly) a decoded sound to mono at `rate`.
SoundClip toClip(const trainz::content::PcmSound& pcm, int rate);

class TrainSound {
public:
    static constexpr int kRate = 22050;

    // Clips by their file name ("idle 1.wav", "up 1 - 2.wav", "horn_loop.wav", ...).
    void addEngineClip(const std::string& name, SoundClip clip) { engine_[name] = std::move(clip); }
    void addHornClip(const std::string& name, SoundClip clip) { horn_[name] = std::move(clip); }
    bool hasEngine() const { return engine_.count("idle 1.wav") != 0; }
    bool hasHorn() const { return horn_.count("horn_loop.wav") != 0 || horn_.count("horn_start.wav") != 0; }

    // The notch the engine should be in (0..), and whether the horn is held.
    void setNotch(int notch);
    void setHorn(bool held);

    // Mixes the next `count` samples into `out` (added, not replaced), at `volume`.
    void mix(float* out, std::size_t count, float volume = 0.6f);

    int engineNotch() const { return current_; }  // the notch whose sound plays (or is coming)
    std::string playing() const;                    // the engine clip now playing (for tests)

private:
    struct Voice {
        const SoundClip* clip = nullptr;
        std::size_t pos = 0;
        bool loop = false;
    };
    const SoundClip* engineClip(const std::string& name) const;
    std::string loopName(int notch) const;
    void nextEngineClip();
    void mixVoice(Voice& v, float* out, std::size_t count, float volume);

    std::map<std::string, SoundClip> engine_, horn_;
    int current_ = 0, target_ = 0;
    Voice engineVoice_;
    std::string engineName_;
    Voice hornVoice_;
    enum class Horn { Off, Start, Loop, End } horn_state_ = Horn::Off;
    bool hornHeld_ = false;
};

// Loads a traincar's engine and horn sounds: the asset KUIDs from its config's `enginesound`
// and `hornsound` keys, then their .wav files. `readFile(kuid, name)` returns a file of an asset.
template <class ReadFile>
void loadTrainSounds(TrainSound& out, std::optional<trainz::content::Kuid> engine, std::optional<trainz::content::Kuid> horn,
                     ReadFile&& readFile) {
    auto load = [&](const trainz::content::Kuid& k, const std::string& name) -> std::optional<SoundClip> {
        const auto bytes = readFile(k, name);
        if (!bytes) {
            return std::nullopt;
        }
        try {
            return toClip(trainz::content::decodeWav(*bytes), TrainSound::kRate);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    };
    if (engine) {
        for (int n = 1; n <= 8; ++n) {
            if (auto c = load(*engine, "idle " + std::to_string(n) + ".wav")) {
                out.addEngineClip("idle " + std::to_string(n) + ".wav", std::move(*c));
            }
            if (n < 8) {
                const std::string up = "up " + std::to_string(n) + " - " + std::to_string(n + 1) + ".wav";
                const std::string down = "down " + std::to_string(n + 1) + " - " + std::to_string(n) + ".wav";
                if (auto c = load(*engine, up)) {
                    out.addEngineClip(up, std::move(*c));
                }
                if (auto c = load(*engine, down)) {
                    out.addEngineClip(down, std::move(*c));
                }
            }
        }
    }
    if (horn) {
        for (const char* name : {"horn_start.wav", "horn_loop.wav", "horn_end.wav"}) {
            if (auto c = load(*horn, name)) {
                out.addHornClip(name, std::move(*c));
            }
        }
    }
}

}  // namespace trainz::game
