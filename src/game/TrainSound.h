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
    // General effects (Data.ja's sounds/...: wheel clacks, wheels over points, the air brake).
    void addEffect(const std::string& name, SoundClip clip) { effects_[name] = std::move(clip); }
    bool hasEffect(const std::string& name) const { return effects_.count(name) != 0; }
    // Plays an effect once, after `delay` samples, at `volume` (up to 16 at a time; the oldest goes).
    void play(const std::string& name, float volume, std::size_t delay = 0);
    // Keeps an effect looping at `volume` while `on` (start/stop clips are played by the caller).
    void loop(const std::string& name, bool on, float volume);
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

    std::map<std::string, SoundClip> engine_, horn_, effects_;
    struct Shot {
        Voice voice;
        float volume = 1;
        std::size_t delay = 0;
    };
    std::vector<Shot> shots_;
    std::map<std::string, Shot> loops_;
    int current_ = 0, target_ = 0;
    Voice engineVoice_;
    std::string engineName_;
    Voice hornVoice_;
    enum class Horn { Off, Start, Loop, End } horn_state_ = Horn::Off;
    bool hornHeld_ = false;
};

// The running sounds of a moving train, from the game's general sounds in Data.ja (OBSERVED
// files: sounds/track/wheel_clang1..6.wav and _slow1/2, sounds/junction/wheels_over_points_1..6.wav,
// sounds/airbrakes/start.wav, idle 1.wav, stop.wav). The port's rules, not the game's: a pair of
// wheel clacks at every rail joint (every kJointSpacing metres, the axles kAxleGap apart), louder
// with speed and the slow clips below 20 km/h; a wheels-over-points clip at a junction; the brake
// hiss while the brake pipe is falling (start, loop, stop).
struct RunningSounds {
    static constexpr double kJointSpacing = 12.0;  // metres between rail joints: a GUESS (US 39 ft rails)
    static constexpr double kAxleGap = 2.4;        // metres between the clacks of one bogie
    double sinceJoint = 0;                         // metres run since the last joint
    int clackIndex = 0, pointsIndex = 0;
    bool hissing = false;
    // One step: `speed` m/s, `moved` metres this step (signed), `seconds` long; `overPoints` when
    // the train ran through a junction; `brakeFalling` when the brake pipe pressure fell.
    void update(TrainSound& s, double speed, double moved, double seconds, bool overPoints, bool brakeFalling);
};

// The general sounds above, from the game's Data.ja (`readFile(name)` returns its bytes).
template <class ReadFile>
void loadRunningSounds(TrainSound& out, ReadFile&& readFile) {
    std::vector<std::string> names;
    for (int i = 1; i <= 6; ++i) {
        names.push_back("sounds/track/wheel_clang" + std::to_string(i) + ".wav");
        names.push_back("sounds/junction/wheels_over_points_" + std::to_string(i) + ".wav");
    }
    for (const char* n : {"sounds/track/wheel_clang_slow1.wav", "sounds/track/wheel_clang_slow2.wav", "sounds/airbrakes/start.wav",
                          "sounds/airbrakes/idle 1.wav", "sounds/airbrakes/stop.wav"}) {
        names.push_back(n);
    }
    for (const auto& n : names) {
        const auto bytes = readFile(n);
        if (!bytes) {
            continue;
        }
        try {
            out.addEffect(n, toClip(trainz::content::decodeWav(*bytes), TrainSound::kRate));
        } catch (const std::exception&) {
        }
    }
}

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
