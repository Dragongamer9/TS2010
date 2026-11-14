#include "TrainSound.h"

#include <algorithm>
#include <cmath>

namespace trainz::game {

SoundClip toClip(const trainz::content::PcmSound& pcm, int rate) {
    SoundClip out;
    const std::size_t ch = pcm.channels;
    if (ch == 0 || pcm.sampleRate == 0 || rate <= 0 || pcm.samples.size() < ch) {
        return out;
    }
    const std::size_t frames = pcm.samples.size() / ch;
    std::vector<float> mono(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        float sum = 0;
        for (std::size_t c = 0; c < ch; ++c) {
            sum += static_cast<float>(pcm.samples[i * ch + c]) / 32768.0f;
        }
        mono[i] = sum / static_cast<float>(ch);
    }
    const double step = static_cast<double>(pcm.sampleRate) / rate;
    const auto n = static_cast<std::size_t>(std::floor(static_cast<double>(frames - 1) / step)) + 1;
    out.samples.resize(std::min<std::size_t>(n, std::size_t{50} * rate * 60));  // at most 50 minutes
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        const double at = static_cast<double>(i) * step;
        const auto k = std::min(static_cast<std::size_t>(at), frames - 1);
        const double f = at - static_cast<double>(k);
        const float b = k + 1 < frames ? mono[k + 1] : mono[k];
        out.samples[i] = static_cast<float>(mono[k] * (1 - f) + b * f);
    }
    return out;
}

const SoundClip* TrainSound::engineClip(const std::string& name) const {
    const auto it = engine_.find(name);
    return it == engine_.end() || it->second.samples.empty() ? nullptr : &it->second;
}

std::string TrainSound::loopName(int notch) const { return "idle " + std::to_string(std::clamp(notch, 1, 8)) + ".wav"; }

std::string TrainSound::playing() const { return engineName_; }

void TrainSound::setNotch(int notch) {
    target_ = std::clamp(notch, 0, 8);
    if (engineVoice_.clip == nullptr) {
        nextEngineClip();
    }
}

void TrainSound::setHorn(bool held) {
    if (held && !hornHeld_ && hasHorn()) {
        horn_state_ = horn_.count("horn_start.wav") != 0 ? Horn::Start : Horn::Loop;
        hornVoice_ = {&horn_.at(horn_state_ == Horn::Start ? "horn_start.wav" : "horn_loop.wav"), 0, horn_state_ == Horn::Loop};
    } else if (!held && hornHeld_ && horn_state_ != Horn::Off) {
        if (horn_.count("horn_end.wav") != 0) {
            horn_state_ = Horn::End;
            hornVoice_ = {&horn_.at("horn_end.wav"), 0, false};
        } else {
            horn_state_ = Horn::Off;
            hornVoice_ = {};
        }
    }
    hornHeld_ = held;
}

// After a loop pass or a clip: one notch step toward the target (its clip, if there is one),
// else the loop of the current notch.
void TrainSound::nextEngineClip() {
    const int shown = std::max(1, current_), wanted = std::max(1, target_);
    if (shown != wanted) {
        const int next = shown + (wanted > shown ? 1 : -1);
        const std::string clip = wanted > shown ? "up " + std::to_string(shown) + " - " + std::to_string(next) + ".wav"
                                                : "down " + std::to_string(shown) + " - " + std::to_string(next) + ".wav";
        current_ = next;
        if (const SoundClip* c = engineClip(clip)) {
            engineVoice_ = {c, 0, false};
            engineName_ = clip;
            return;
        }
    }
    current_ = target_;
    if (const SoundClip* c = engineClip(loopName(current_))) {
        engineVoice_ = {c, 0, true};
        engineName_ = loopName(current_);
    } else {
        engineVoice_ = {};
        engineName_.clear();
    }
}

void TrainSound::mixVoice(Voice& v, float* out, std::size_t count, float volume) {
    std::size_t i = 0;
    while (i < count && v.clip != nullptr && !v.clip->samples.empty()) {
        const auto& s = v.clip->samples;
        const std::size_t n = std::min(count - i, s.size() - std::min(v.pos, s.size()));
        for (std::size_t k = 0; k < n; ++k) {
            out[i + k] += volume * s[v.pos + k];
        }
        i += n;
        v.pos += n;
        if (v.pos >= s.size()) {
            if (v.loop) {
                v.pos = 0;  // a notch change leaves the loop at once (in mix), so just go round
            } else if (&v == &engineVoice_) {
                nextEngineClip();
            } else {  // the horn
                if (horn_state_ == Horn::Start && horn_.count("horn_loop.wav") != 0) {
                    horn_state_ = Horn::Loop;
                    v = {&horn_.at("horn_loop.wav"), 0, true};
                } else {
                    horn_state_ = Horn::Off;
                    v = {};
                }
            }
        }
    }
}

void TrainSound::mix(float* out, std::size_t count, float volume) {
    if (out == nullptr || count == 0) {
        return;
    }
    // A notch change is heard at once: leave the loop for the step clip.
    if (engineVoice_.loop && std::max(1, target_) != std::max(1, current_)) {
        nextEngineClip();
    }
    mixVoice(engineVoice_, out, count, volume);
    mixVoice(hornVoice_, out, count, volume);
}

}  // namespace trainz::game
