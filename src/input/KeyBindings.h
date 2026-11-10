// Key bindings: UserData/settings/keyboard.txt. See docs/formats/keyboard.md.
//
// The file uses the config.txt syntax: groups (driver, surveyor, menu, ...)
// of "action  combo, combo, ..." lines, where a combo is optional modifiers
// (shift, ctrl/control, alt) followed by one key name, and "null" means no key.
//
// Key names follow the game's own table in trainz.exe (around file offset
// 0x543b10): display names such as "Page Down" / "KP/" and typing aliases such
// as "pgdn", "pagedown", "kpdn", "fullstop", "period", compared ignoring case.
// Single printable characters ("'", "-", "[", "0") are keys too, as in the file.
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
    std::string name;  // e.g. "train_cabin_throttle_up"
    std::vector<KeyCombo> combos;  // empty for "null"
};

struct KeyBindings {
    std::map<std::string, std::vector<KeyAction>> groups;  // group name -> actions in file order
    std::vector<std::string> warnings;

    const KeyAction* find(std::string_view group, std::string_view action) const;
};

// The canonical name for a key name or alias, or nullopt if it is not a key.
std::optional<std::string> canonicalKey(std::string_view name);

// "ctrl+shift+F5" style text, for display.
std::string describe(const KeyCombo& combo);

// Parses keyboard.txt. Throws trainz::content::ContentError on syntax errors;
// unknown key names become warnings, and that combo is skipped. Extra words
// before the key that are not modifiers are ignored with a warning and the last
// word is taken as the key: an unconfirmed guess (see docs/formats/keyboard.md).
KeyBindings parseKeyBindings(std::span<const std::uint8_t> data);

}  // namespace trainz::input
