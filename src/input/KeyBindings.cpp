#include "KeyBindings.h"

#include "content/AssetConfig.h"
#include "content/ContentError.h"

#include <algorithm>
#include <array>

namespace trainz::input {

namespace {

// Generated from the compare chain in trainz.exe (VA 0x4d4b60-0x4d5a5e): each entry
// is the string passed to Jet::AnsiString::operator== and the byte then stored at
// key+2. 80 of the codes are DirectInput DIK_ codes + 1; 17 extended keys are packed
// into 0x55-0x6b in DIK_ order, and Pause is 0x02.
constexpr std::array<KeyName, 141> kGameKeyNames = {{
    {"tilde", "Grave", 0x2a},
    {"`", "Grave", 0x2a},
    {"minus", "Minus", 0x0d},
    {"underscore", "Minus", 0x0d},
    {"plus", "Equals", 0x0e},
    {"equals", "Equals", 0x0e},
    {"=", "Equals", 0x0e},
    {"backspace", "Backspace", 0x0f},
    {"space", "Space", 0x3a},
    {"esc", "Esc", 0x01},
    {"escape", "Esc", 0x01},
    {"f1", "F1", 0x3c},
    {"f2", "F2", 0x3d},
    {"f3", "F3", 0x3e},
    {"f4", "F4", 0x3f},
    {"f5", "F5", 0x40},
    {"f6", "F6", 0x41},
    {"f7", "F7", 0x42},
    {"f8", "F8", 0x43},
    {"f9", "F9", 0x44},
    {"f10", "F10", 0x45},
    {"f11", "F11", 0x55},
    {"f12", "F12", 0x56},
    {"printscreen", "PrintScreen", 0x6b},
    {"prtscn", "PrintScreen", 0x6b},
    {"sysrq", "PrintScreen", 0x6b},
    {"scrlk", "ScrollLock", 0x47},
    {"scrolllock", "ScrollLock", 0x47},
    {"pause", "Pause", 0x02},
    {"break", "Pause", 0x02},
    {"home", "Home", 0x5e},
    {"end", "End", 0x63},
    {"del", "Delete", 0x67},
    {"delete", "Delete", 0x67},
    {"pgup", "PageUp", 0x60},
    {"pageup", "PageUp", 0x60},
    {"pgdn", "PageDown", 0x65},
    {"pagedown", "PageDown", 0x65},
    {"ins", "Insert", 0x66},
    {"insert", "Insert", 0x66},
    {"up", "Up", 0x5f},
    {"down", "Down", 0x64},
    {"left", "Left", 0x61},
    {"right", "Right", 0x62},
    {"numlock", "NumLock", 0x46},
    {"kpslash", "KPSlash", 0x5b},
    {"kpstar", "KPStar", 0x38},
    {"kpminus", "KPMinus", 0x4b},
    {"kpplus", "KPPlus", 0x4f},
    {"kpenter", "KPEnter", 0x5a},
    {"kpperiod", "KPPeriod", 0x54},
    {"kpdot", "KPPeriod", 0x54},
    {"kp0", "KP0", 0x53},
    {"kp1", "KP1", 0x50},
    {"kp2", "KP2", 0x51},
    {"kp3", "KP3", 0x52},
    {"kp4", "KP4", 0x4c},
    {"kp5", "KP5", 0x4d},
    {"kp6", "KP6", 0x4e},
    {"kp7", "KP7", 0x48},
    {"kp8", "KP8", 0x49},
    {"kp9", "KP9", 0x4a},
    {"kpend", "KP1", 0x50},
    {"kppgdn", "KP3", 0x52},
    {"kppagedown", "KP3", 0x52},
    {"kphome", "KP7", 0x48},
    {"kppgup", "KP9", 0x4a},
    {"kppageup", "KP9", 0x4a},
    {"kpup", "KP8", 0x49},
    {"kpdown", "KP2", 0x51},
    {"kpdn", "KP2", 0x51},
    {"kpleft", "KP4", 0x4c},
    {"kpclear", "KP5", 0x4d},
    {"kpclr", "KP5", 0x4d},
    {"kpright", "KP6", 0x4e},
    {"kpins", "KP0", 0x53},
    {"kpinsert", "KP0", 0x53},
    {"tab", "Tab", 0x10},
    {"enter", "Enter", 0x1d},
    {"comma", "Comma", 0x34},
    {"period", "Period", 0x35},
    {"fullstop", "Period", 0x35},
    {"slash", "Slash", 0x36},
    {"backslash", "Backslash", 0x2c},
    {"semi", "Semicolon", 0x28},
    {"semicolon", "Semicolon", 0x28},
    {"quote", "Apostrophe", 0x29},
    {"apostrophe", "Apostrophe", 0x29},
    {"'", "Apostrophe", 0x29},
    {"openbrace", "LeftBracket", 0x1b},
    {"openbracket", "LeftBracket", 0x1b},
    {"leftbracket", "LeftBracket", 0x1b},
    {"[", "LeftBracket", 0x1b},
    {"closedbrace", "RightBracket", 0x1c},
    {"closedbracket", "RightBracket", 0x1c},
    {"rightbracket", "RightBracket", 0x1c},
    {"]", "RightBracket", 0x1c},
    {"a", "A", 0x1f},
    {"b", "B", 0x31},
    {"c", "C", 0x2f},
    {"d", "D", 0x21},
    {"e", "E", 0x13},
    {"f", "F", 0x22},
    {"g", "G", 0x23},
    {"h", "H", 0x24},
    {"i", "I", 0x18},
    {"j", "J", 0x25},
    {"k", "K", 0x26},
    {"l", "L", 0x27},
    {"m", "M", 0x33},
    {"n", "N", 0x32},
    {"o", "O", 0x19},
    {"p", "P", 0x1a},
    {"q", "Q", 0x11},
    {"r", "R", 0x14},
    {"s", "S", 0x20},
    {"t", "T", 0x15},
    {"u", "U", 0x17},
    {"v", "V", 0x30},
    {"w", "W", 0x12},
    {"x", "X", 0x2e},
    {"y", "Y", 0x16},
    {"z", "Z", 0x2d},
    {"leftshiftkey", "LeftShift", 0x2b},
    {"rightshiftkey", "RightShift", 0x37},
    {"leftcontrolkey", "LeftCtrl", 0x1e},
    {"rightcontrolkey", "RightCtrl", 0x5c},
    {"-", "Minus", 0x0d},
    {"/", "Slash", 0x36},
    {"\\", "Backslash", 0x2c},
    {";", "Semicolon", 0x28},
    {"0", "0", 0x0c},
    {"1", "1", 0x03},
    {"2", "2", 0x04},
    {"3", "3", 0x05},
    {"4", "4", 0x06},
    {"5", "5", 0x07},
    {"6", "6", 0x08},
    {"7", "7", 0x09},
    {"8", "8", 0x0a},
    {"9", "9", 0x0b},
}};

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

}  // namespace

std::span<const KeyName> gameKeyNames() {
    return kGameKeyNames;
}

std::optional<std::string> canonicalKey(std::string_view name) {
    const std::string n = lower(name);
    for (const auto& k : kGameKeyNames) {
        if (n == k.name) {
            return std::string(k.canonical);
        }
    }
    return std::nullopt;
}

std::optional<std::uint8_t> engineKeyCode(std::string_view canonical) {
    for (const auto& k : kGameKeyNames) {
        if (canonical == k.canonical) {
            return k.engineCode;
        }
    }
    return std::nullopt;
}

std::vector<std::string> allCanonicalKeys() {
    std::vector<std::string> out;
    for (const auto& k : kGameKeyNames) {
        if (std::find(out.begin(), out.end(), k.canonical) == out.end()) {
            out.emplace_back(k.canonical);
        }
    }
    return out;
}

bool isModifierKey(std::string_view k) {
    return k == "LeftShift" || k == "RightShift" || k == "LeftCtrl" || k == "RightCtrl";
}

std::string describe(const KeyCombo& c) {
    std::string s;
    if (c.ctrl) {
        s += "ctrl+";
    }
    if (c.alt) {
        s += "alt+";
    }
    if (c.shift) {
        s += "shift+";
    }
    return s + c.key;
}

const KeyAction* KeyBindings::find(std::string_view group, std::string_view action) const {
    const auto g = groups.find(std::string(group));
    if (g == groups.end()) {
        return nullptr;
    }
    for (const auto& a : g->second) {
        if (a.name == action) {
            return &a;
        }
    }
    return nullptr;
}

std::vector<KeyCombo> parseComboList(std::string_view text, const std::string& context,
                                     std::vector<std::string>& warnings) {
    std::vector<KeyCombo> combos;
    KeyCombo cur;
    std::string word;
    auto applyWord = [&] {
        if (word.empty()) {
            return;  // trainz.exe returns early on an empty word
        }
        if (word == "null") {
            cur = KeyCombo{};
        } else if (word == "ctrl" || word == "control") {
            cur.ctrl = true;
        } else if (word == "alt") {
            cur.alt = true;
        } else if (word == "shift") {
            cur.shift = true;
        } else if (auto key = canonicalKey(word)) {
            if (!cur.key.empty() && cur.key != *key) {
                warnings.push_back(context + ": key '" + cur.key + "' replaced by '" + *key +
                                   "' (the game does the same: the last key word wins)");
            }
            cur.key = std::move(*key);
        } else {
            warnings.push_back(context + ": unknown key name '" + word +
                               "'; the game clears this combo, and so does the reader");
            cur = KeyCombo{};
        }
        word.clear();
    };
    auto endCombo = [&] {
        applyWord();
        // The game keeps key-less combos too; they can never match a key, so they are dropped.
        if (!cur.key.empty() && std::find(combos.begin(), combos.end(), cur) == combos.end()) {
            combos.push_back(cur);
        }
        cur = KeyCombo{};
    };
    for (const char ch : text) {
        if (ch == ',') {
            endCombo();
        } else if (ch == ' ') {
            applyWord();
        } else {
            word += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
        }
    }
    endCombo();
    return combos;
}

KeyBindings parseKeyBindings(std::span<const std::uint8_t> data) {
    using trainz::content::ConfigType;
    const auto cfg = trainz::content::parseConfigText(data);
    KeyBindings kb;
    kb.warnings = cfg.warnings;
    for (const auto& group : cfg.root.children) {
        if (group.type != ConfigType::Group) {
            kb.warnings.push_back("'" + group.key + "' at the top level is not a group; ignored");
            continue;
        }
        auto& actions = kb.groups[group.key];
        for (const auto& item : group.children) {
            KeyAction action;
            action.name = item.key;
            const std::string context = group.key + "/" + item.key;
            std::string text;
            if (item.type == ConfigType::String) {
                text = item.text;
            } else if (item.type == ConfigType::Integers) {
                // A digit key ("1") reads as a number in config syntax; put the text back.
                for (std::size_t i = 0; i < item.ints.size(); ++i) {
                    text += (i == 0 ? "" : ",") + std::to_string(item.ints[i]);
                }
            } else if (item.type != ConfigType::Null) {
                kb.warnings.push_back(context + ": value is not a key list; ignored");
            }
            action.combos = parseComboList(text, context, kb.warnings);
            actions.push_back(std::move(action));
        }
    }
    return kb;
}

}  // namespace trainz::input
