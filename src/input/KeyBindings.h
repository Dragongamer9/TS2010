// Key bindings: UserData/settings/keyboard.txt. See docs/formats/keyboard.md.
//
// The file uses the config.txt syntax: groups (driver, surveyor, menu, ...) of
// "action  combo, combo, ..." lines. Each combo is read the way trainz.exe reads it
// (traced: splitter at VA 0x4d5bb0, word parser at VA 0x4d4b60):
// - the text is lowercased (tolower), ',' ends a combo and ' ' ends a word; nothing
//   else separates (a tab is part of a word);
// - words are applied in order to one combo: "ctrl", "control", "alt", "shift" add
//   a modifier; a key name sets the key and replaces any earlier key; "null" or an
//   unknown word clears the whole combo (key and modifiers); empty words do nothing.
// Key names are exactly the 141 the game's word parser compares against.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trainz::input {

struct KeyCombo {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    std::string key;  // canonical name, e.g. "A", "PageUp", "KP9", "Space", "F12"

    bool operator==(const KeyCombo&) const = default;
};

struct KeyAction {
    std::string name;              // e.g. "train_cabin_throttle_up"
    std::vector<KeyCombo> combos;  // combos with no key (null, cleared) are left out
};

struct KeyBindings {
    std::map<std::string, std::vector<KeyAction>> groups;  // group name -> actions in file order
    std::vector<std::string> warnings;

    const KeyAction* find(std::string_view group, std::string_view action) const;
};

// One name the game's parser accepts.
struct KeyName {
    const char* name;       // lowercase, as compared in trainz.exe
    const char* canonical;  // the port's name for the key
    std::uint8_t engineCode;  // the byte trainz.exe stores for it (traced)
};

// All 141 names, in the order trainz.exe compares them.
std::span<const KeyName> gameKeyNames();

// The canonical key for a game key name (any case), or nullopt.
std::optional<std::string> canonicalKey(std::string_view name);

// The engine's key code for a canonical key, or nullopt.
std::optional<std::uint8_t> engineKeyCode(std::string_view canonical);

// Every canonical key (98), each once.
std::vector<std::string> allCanonicalKeys();

// True for LeftShift/RightShift/LeftCtrl/RightCtrl (the only modifier keys the game names).
bool isModifierKey(std::string_view canonical);

// "ctrl+alt+shift+F5" style text, for display (not a game name).
std::string describe(const KeyCombo& combo);

// Reads one combo list ("q, KP9, KPPgUp") the way the game does. Unknown words and
// replaced keys are reported through warnings, prefixed with context.
std::vector<KeyCombo> parseComboList(std::string_view text, const std::string& context,
                                     std::vector<std::string>& warnings);

// Parses keyboard.txt. Throws trainz::content::ContentError on config syntax errors.
KeyBindings parseKeyBindings(std::span<const std::uint8_t> data);

}  // namespace trainz::input
