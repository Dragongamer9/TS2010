// Canonical key names (KeyBindings.h) <-> SDL2 scancodes.
//
// The port binds to SDL scancodes (physical key positions). Evidence that the game
// does the same: its word parser stores a one-byte engine code per key name, and
// those codes are DirectInput DIK_ scancodes + 1 for 80 of the 98 keys, with the
// other 18 extended keys packed in DIK_ order (Pause aside, at 0x02). So keys are numbered by
// position, not by character. Not traced: where the game turns a live DIK_ code
// into an engine code (no lookup table was found in the input DLLs), so a key that
// sits differently on a non-US keyboard is not proven to behave the same.
// Keys the game has no name for (Caps Lock, Alt keys, Windows keys, F13+) map to nothing.
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
// (LeftShift, LeftCtrl, ...), that modifier's flag is left out, so "leftshiftkey" matches.
std::optional<KeyCombo> comboFromKey(SDL_Scancode code, Uint16 mods);

}  // namespace trainz::input
