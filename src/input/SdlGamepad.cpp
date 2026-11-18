#include "SdlGamepad.h"

namespace trainz::input {

bool GamepadSlot::openIfNone() {
    for (int j = 0; pad_ == nullptr && j < SDL_NumJoysticks(); ++j) {
        if (SDL_IsGameController(j)) {
            pad_ = SDL_GameControllerOpen(j);
        }
    }
    return pad_ != nullptr;
}

void GamepadSlot::removed(SDL_JoystickID which) {
    if (isOurs(which)) {
        close();
        openIfNone();  // another one already plugged in takes over
    }
}

bool GamepadSlot::isOurs(SDL_JoystickID which) const {
    return pad_ != nullptr && which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_));
}

void GamepadSlot::close() {
    if (pad_ != nullptr) {
        SDL_GameControllerClose(pad_);
        pad_ = nullptr;
    }
}

}  // namespace trainz::input
