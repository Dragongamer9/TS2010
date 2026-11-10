// Canonical key names (KeyBindings.h) <-> SDL2 scancodes.
//
// The port binds to SDL scancodes (physical key positions), so a binding stays on
// the same key whatever the keyboard layout. This is the port's choice. The game
// imports DINPUT8.dll, but that alone doesn't show how it maps names to keys
// (DIK_ codes, virtual keys or characters, and on which layout); that code is not
// traced. Each name's key is assumed from the name on a US layout.
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
