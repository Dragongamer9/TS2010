// Canonical key names (KeyBindings.h) <-> SDL2 scancodes.
//
// Scancodes name physical key positions, as DirectInput 8's DIK_ codes do (the game
// imports DINPUT8.dll), so a binding stays on the same key whatever the keyboard
// layout. Which DIK code each game name stands for is assumed from the names on a
// US layout; the game's name-to-code table has not been traced.
//
// Needs SDL2 (libsdl2-dev); built only when SDL2 is found.
#pragma once

#include "input/KeyBindings.h"

#include <SDL_keyboard.h>
#include <SDL_scancode.h>

#include <optional>
#include <string>
#include <string_view>

namespace trainz::input {

std::optional<SDL_Scancode> scancodeFor(std::string_view canonical);
std::optional<std::string> canonicalFor(SDL_Scancode code);

// The combo a key-down produces. When the key is itself a modifier
// (LeftShift, ...), that modifier's flag is left out, so "leftshiftkey" matches.
std::optional<KeyCombo> comboFromKey(SDL_Scancode code, Uint16 mods);

}  // namespace trainz::input
