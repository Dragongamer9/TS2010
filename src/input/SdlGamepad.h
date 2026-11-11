// Which gamepad a viewer listens to: the first SDL game controller, handed over to another
// connected one when it is unplugged; events from other gamepads are not its own. Needs
// SDL's game controller subsystem initialised.
#pragma once

#include <SDL.h>

namespace trainz::input {

class GamepadSlot {
public:
    GamepadSlot() = default;
    GamepadSlot(const GamepadSlot&) = delete;
    GamepadSlot& operator=(const GamepadSlot&) = delete;
    ~GamepadSlot() { close(); }

    // Opens the first game controller if none is open. True when one is open afterwards.
    bool openIfNone();
    // A controller was unplugged (SDL_CONTROLLERDEVICEREMOVED's `which`): if it was ours, close
    // it and take the next connected one.
    void removed(SDL_JoystickID which);
    // Whether an event with this instance id (cbutton/caxis `which`) comes from our gamepad.
    bool isOurs(SDL_JoystickID which) const;
    SDL_GameController* get() const { return pad_; }
    void close();

private:
    SDL_GameController* pad_ = nullptr;
};

}  // namespace trainz::input
