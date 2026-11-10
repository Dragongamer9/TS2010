#include "KeyBindings.h"

#include "content/AssetConfig.h"
#include "content/ContentError.h"

#include <algorithm>
#include <cctype>

namespace trainz::input {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

struct KeyName {
    const char* canonical;
    std::vector<const char*> names;  // lowercase display names and aliases from trainz.exe
};

const std::vector<KeyName>& keyTable() {
    static const std::vector<KeyName> table = {
        {"Esc", {"esc", "escape"}},
        {"Tab", {"tab"}},
        {"Enter", {"enter"}},
        {"Space", {"space"}},
        {"Backspace", {"back", "backspace"}},
        {"Pause", {"pause", "break"}},
        {"PrintScreen", {"printscreen", "prtscn", "sysrq"}},
        {"ScrollLock", {"scroll lock", "scrlk", "scrolllock"}},
        {"NumLock", {"num lock", "numlock"}},
        {"CapsLock", {"caps lock"}},
        {"Home", {"home"}},
        {"End", {"end"}},
        {"PageUp", {"page up", "pgup", "pageup"}},
        {"PageDown", {"page down", "pgdn", "pagedown"}},
        {"Insert", {"ins", "insert"}},
        {"Delete", {"del", "delete"}},
        {"Up", {"up"}},
        {"Down", {"down"}},
        {"Left", {"left"}},
        {"Right", {"right"}},
        {"LeftShift", {"left shift", "leftshiftkey"}},
        {"RightShift", {"right shift", "rightshiftkey"}},
        {"LeftCtrl", {"left ctrl", "leftcontrolkey"}},
        {"RightCtrl", {"right ctrl", "rightcontrolkey"}},
        {"LeftAlt", {"left alt"}},
        {"RightAlt", {"right alt"}},
        {"LeftWin", {"left win"}},
        {"RightWin", {"right win"}},
        {"Menu", {"app menu"}},
        {"Grave", {"`", "~", "tilde"}},
        {"Minus", {"-", "minus", "underscore"}},
        {"Equals", {"=", "equals", "plus"}},
        {"LeftBracket", {"[", "openbrace", "openbracket", "leftbracket"}},
        {"RightBracket", {"]", "closedbrace", "closedbracket", "rightbracket"}},
        {"Backslash", {"\\", "backslash"}},
        {"Semicolon", {";", "semi", "semicolon"}},
        {"Apostrophe", {"'", "quote", "apostrophe"}},
        {"Comma", {",", "comma"}},
        {"Period", {".", "period", "fullstop"}},
        {"Slash", {"/", "slash"}},
        {"KPSlash", {"kp/", "kpslash"}},
        {"KPStar", {"kp*", "kpstar"}},
        {"KPMinus", {"kp-", "kpminus"}},
        {"KPPlus", {"kp+", "kpplus"}},
        {"KPEnter", {"kpenter"}},
        {"KPPeriod", {"kp.", "kpperiod", "kpdot", "kpdel"}},
        {"KP0", {"kp0", "kpins", "kpinsert"}},
        {"KP1", {"kp1", "kpend"}},
        {"KP2", {"kp2", "kpdown", "kpdn"}},
        {"KP3", {"kp3", "kppgdn", "kppagedown"}},
        {"KP4", {"kp4", "kpleft"}},
        {"KP5", {"kp5", "kpclear", "kpclr"}},
        {"KP6", {"kp6", "kpright"}},
        {"KP7", {"kp7", "kphome"}},
        {"KP8", {"kp8", "kpup"}},
        {"KP9", {"kp9", "kppgup", "kppageup"}},
    };
    return table;
}

}  // namespace

std::optional<std::string> canonicalKey(std::string_view name) {
    const std::string n = lower(name);
    if (n.empty()) {
        return std::nullopt;
    }
    // Letters and digits: "a".."z", "0".."9".
    if (n.size() == 1 && ((n[0] >= 'a' && n[0] <= 'z') || (n[0] >= '0' && n[0] <= '9'))) {
        return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(n[0]))));
    }
    // Function keys F1..F15.
    if (n.size() >= 2 && n.size() <= 3 && n[0] == 'f') {
        int v = 0;
        bool digits = true;
        for (std::size_t i = 1; i < n.size(); ++i) {
            digits = digits && n[i] >= '0' && n[i] <= '9';
            v = v * 10 + (n[i] - '0');
        }
        if (digits && v >= 1 && v <= 15) {
            return "F" + std::to_string(v);
        }
    }
    for (const auto& k : keyTable()) {
        // The port's own canonical names are accepted too (so describe() output reads
        // back); the game's files only use the names from its table.
        if (n == lower(k.canonical)) {
            return std::string(k.canonical);
        }
        for (const char* alias : k.names) {
            if (n == alias) {
                return std::string(k.canonical);
            }
        }
    }
    return std::nullopt;
}

std::vector<std::string> allCanonicalKeys() {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'Z'; ++c) {
        out.emplace_back(1, c);
    }
    for (char c = '0'; c <= '9'; ++c) {
        out.emplace_back(1, c);
    }
    for (int f = 1; f <= 15; ++f) {
        out.push_back("F" + std::to_string(f));
    }
    for (const auto& k : keyTable()) {
        out.emplace_back(k.canonical);
    }
    return out;
}

bool isModifierKey(std::string_view k) {
    return k == "LeftShift" || k == "RightShift" || k == "LeftCtrl" || k == "RightCtrl" || k == "LeftAlt" ||
           k == "RightAlt";
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

KeyBindings parseKeyBindings(std::span<const std::uint8_t> data) {
    const auto cfg = trainz::content::parseConfigText(data);
    KeyBindings kb;
    kb.warnings = cfg.warnings;
    for (const auto& group : cfg.root.children) {
        if (group.type != trainz::content::ConfigType::Group) {
            kb.warnings.push_back("'" + group.key + "' at the top level is not a group; ignored");
            continue;
        }
        auto& actions = kb.groups[group.key];
        for (const auto& item : group.children) {
            KeyAction action;
            action.name = item.key;
            const std::string value = item.type == trainz::content::ConfigType::String ? item.text : std::string();
            if (item.type != trainz::content::ConfigType::String && item.type != trainz::content::ConfigType::Null) {
                // A lone digit key ("1") parses as a number: use its text form.
                if (item.type == trainz::content::ConfigType::Integers && item.ints.size() == 1) {
                    action.combos.push_back({false, false, false, std::to_string(item.ints[0])});
                } else {
                    kb.warnings.push_back(group.key + "/" + item.key + ": value is not a key list; ignored");
                }
                actions.push_back(std::move(action));
                continue;
            }
            std::size_t start = 0;
            while (start <= value.size()) {
                std::size_t comma = value.find(',', start);
                if (comma == std::string::npos) {
                    comma = value.size();
                }
                const std::string combo = value.substr(start, comma - start);
                start = comma + 1;
                std::vector<std::string> words;
                std::size_t p = 0;
                while (p < combo.size()) {
                    while (p < combo.size() && (combo[p] == ' ' || combo[p] == '\t')) {
                        ++p;
                    }
                    std::size_t q = p;
                    while (q < combo.size() && combo[q] != ' ' && combo[q] != '\t') {
                        ++q;
                    }
                    if (q > p) {
                        words.push_back(combo.substr(p, q - p));
                    }
                    p = q;
                }
                if (words.empty() || (words.size() == 1 && lower(words[0]) == "null")) {
                    continue;
                }
                KeyCombo kc;
                bool bad = false;
                for (std::size_t i = 0; i + 1 < words.size(); ++i) {
                    const std::string m = lower(words[i]);
                    if (m == "shift") {
                        kc.shift = true;
                    } else if (m == "ctrl" || m == "control") {
                        kc.ctrl = true;
                    } else if (m == "alt") {
                        kc.alt = true;
                    } else {
                        kb.warnings.push_back(group.key + "/" + item.key + ": '" + words[i] +
                                              "' is not a modifier; ignored, and the last word taken as the key "
                                              "(an unconfirmed guess; the game's reading is not traced)");
                    }
                }
                const auto key = canonicalKey(words.back());
                if (!key) {
                    kb.warnings.push_back(group.key + "/" + item.key + ": unknown key '" + words.back() + "'");
                    bad = true;
                }
                if (!bad) {
                    kc.key = *key;
                    // "KP9, KPPgUp" name the same key twice; keep one.
                    if (std::find(action.combos.begin(), action.combos.end(), kc) == action.combos.end()) {
                        action.combos.push_back(kc);
                    }
                }
            }
            actions.push_back(std::move(action));
        }
    }
    return kb;
}

}  // namespace trainz::input
