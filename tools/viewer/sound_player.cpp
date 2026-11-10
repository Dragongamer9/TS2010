// sound-player: play a .wav or .mp3 from a Trainz archive through SDL2 audio
// (read-only). MP3 needs a build with libmpg123.
//
//   sound-player ARCHIVE.ja NAME
#include "content/ContentError.h"
#include "content/JaArchive.h"
#include "content/SoundFile.h"

#include <SDL.h>

#include <iostream>

using namespace trainz::content;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: sound-player ARCHIVE.ja NAME\n";
        return 2;
    }
    PcmSound s;
    try {
        const auto archive = JaArchive::open(argv[1]);
        const JaEntry* e = archive.find(argv[2]);
        if (e == nullptr) {
            std::cerr << "'" << argv[2] << "' is not in " << argv[1] << "\n";
            return 1;
        }
        s = decodeSoundFile(e->name, archive.readLimited(*e, std::size_t{256} << 20));
        for (const auto& m : s.warnings) {
            std::cerr << "warning: " << m << "\n";
        }
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
    if (s.samples.empty()) {
        std::cerr << "the sound is empty\n";
        return 1;
    }
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_AudioSpec want{};
    want.freq = static_cast<int>(s.sampleRate);
    want.format = AUDIO_S16SYS;
    want.channels = static_cast<Uint8>(s.channels);
    want.samples = 4096;
    SDL_AudioSpec have{};
    const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev == 0) {
        std::cerr << "cannot open an audio device: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    SDL_QueueAudio(dev, s.samples.data(), static_cast<Uint32>(s.samples.size() * sizeof(std::int16_t)));
    SDL_PauseAudioDevice(dev, 0);
    std::cout << "playing " << s.samples.size() / s.channels << " frames at " << s.sampleRate << " Hz...\n";
    while (SDL_GetQueuedAudioSize(dev) > 0) {
        SDL_Delay(50);
    }
    SDL_Delay(200);
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
