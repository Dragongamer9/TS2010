#include "input/SdlKeys.h"

#include <array>
#include <utility>

namespace trainz::input {

namespace {

struct Pair {
    const char* name;
    SDL_Scancode code;
};

constexpr std::array kTable = {
    Pair{"Esc", SDL_SCANCODE_ESCAPE},
    Pair{"Tab", SDL_SCANCODE_TAB},
    Pair{"Enter", SDL_SCANCODE_RETURN},
    Pair{"Space", SDL_SCANCODE_SPACE},
    Pair{"Backspace", SDL_SCANCODE_BACKSPACE},
    Pair{"Pause", SDL_SCANCODE_PAUSE},
    Pair{"PrintScreen", SDL_SCANCODE_PRINTSCREEN},
    Pair{"ScrollLock", SDL_SCANCODE_SCROLLLOCK},
    Pair{"NumLock", SDL_SCANCODE_NUMLOCKCLEAR},
    Pair{"CapsLock", SDL_SCANCODE_CAPSLOCK},
    Pair{"Home", SDL_SCANCODE_HOME},
    Pair{"End", SDL_SCANCODE_END},
    Pair{"PageUp", SDL_SCANCODE_PAGEUP},
    Pair{"PageDown", SDL_SCANCODE_PAGEDOWN},
    Pair{"Insert", SDL_SCANCODE_INSERT},
    Pair{"Delete", SDL_SCANCODE_DELETE},
    Pair{"Up", SDL_SCANCODE_UP},
    Pair{"Down", SDL_SCANCODE_DOWN},
    Pair{"Left", SDL_SCANCODE_LEFT},
    Pair{"Right", SDL_SCANCODE_RIGHT},
    Pair{"LeftShift", SDL_SCANCODE_LSHIFT},
    Pair{"RightShift", SDL_SCANCODE_RSHIFT},
    Pair{"LeftCtrl", SDL_SCANCODE_LCTRL},
    Pair{"RightCtrl", SDL_SCANCODE_RCTRL},
    Pair{"LeftAlt", SDL_SCANCODE_LALT},
    Pair{"RightAlt", SDL_SCANCODE_RALT},
    Pair{"LeftWin", SDL_SCANCODE_LGUI},
    Pair{"RightWin", SDL_SCANCODE_RGUI},
    Pair{"Menu", SDL_SCANCODE_APPLICATION},
    Pair{"Grave", SDL_SCANCODE_GRAVE},
    Pair{"Minus", SDL_SCANCODE_MINUS},
    Pair{"Equals", SDL_SCANCODE_EQUALS},
    Pair{"LeftBracket", SDL_SCANCODE_LEFTBRACKET},
    Pair{"RightBracket", SDL_SCANCODE_RIGHTBRACKET},
    Pair{"Backslash", SDL_SCANCODE_BACKSLASH},
    Pair{"Semicolon", SDL_SCANCODE_SEMICOLON},
    Pair{"Apostrophe", SDL_SCANCODE_APOSTROPHE},
    Pair{"Comma", SDL_SCANCODE_COMMA},
    Pair{"Period", SDL_SCANCODE_PERIOD},
    Pair{"Slash", SDL_SCANCODE_SLASH},
    Pair{"KPSlash", SDL_SCANCODE_KP_DIVIDE},
    Pair{"KPStar", SDL_SCANCODE_KP_MULTIPLY},
    Pair{"KPMinus", SDL_SCANCODE_KP_MINUS},
    Pair{"KPPlus", SDL_SCANCODE_KP_PLUS},
    Pair{"KPEnter", SDL_SCANCODE_KP_ENTER},
    Pair{"KPPeriod", SDL_SCANCODE_KP_PERIOD},
    Pair{"KP0", SDL_SCANCODE_KP_0},
    Pair{"KP1", SDL_SCANCODE_KP_1},
    Pair{"KP2", SDL_SCANCODE_KP_2},
    Pair{"KP3", SDL_SCANCODE_KP_3},
    Pair{"KP4", SDL_SCANCODE_KP_4},
    Pair{"KP5", SDL_SCANCODE_KP_5},
    Pair{"KP6", SDL_SCANCODE_KP_6},
    Pair{"KP7", SDL_SCANCODE_KP_7},
    Pair{"KP8", SDL_SCANCODE_KP_8},
    Pair{"KP9", SDL_SCANCODE_KP_9},
    Pair{"F13", SDL_SCANCODE_F13},
    Pair{"F14", SDL_SCANCODE_F14},
    Pair{"F15", SDL_SCANCODE_F15},
};

}  // namespace

std::optional<SDL_Scancode> scancodeFor(std::string_view k) {
    if (k.size() == 1 && k[0] >= 'A' && k[0] <= 'Z') {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (k[0] - 'A'));
    }
    if (k.size() == 1 && k[0] >= '1' && k[0] <= '9') {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (k[0] - '1'));
    }
    if (k == "0") {
        return SDL_SCANCODE_0;
    }
    if (k.size() >= 2 && k.size() <= 3 && k[0] == 'F') {
        int f = 0;
        for (std::size_t i = 1; i < k.size(); ++i) {
            if (k[i] < '0' || k[i] > '9') {
                f = -1;
                break;
            }
            f = f * 10 + (k[i] - '0');
        }
        if (f >= 1 && f <= 12) {
            return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + (f - 1));
        }
    }
    for (const auto& p : kTable) {
        if (k == p.name) {
            return p.code;
        }
    }
    return std::nullopt;
}

std::optional<std::string> canonicalFor(SDL_Scancode code) {
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
        return std::string(1, static_cast<char>('A' + (code - SDL_SCANCODE_A)));
    }
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
        return std::string(1, static_cast<char>('1' + (code - SDL_SCANCODE_1)));
    }
    if (code == SDL_SCANCODE_0) {
        return std::string("0");
    }
    if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) {
        return "F" + std::to_string(1 + (code - SDL_SCANCODE_F1));
    }
    for (const auto& p : kTable) {
        if (code == p.code) {
            return std::string(p.name);
        }
    }
    return std::nullopt;
}

std::optional<KeyCombo> comboFromKey(SDL_Scancode code, Uint16 mods) {
    auto name = canonicalFor(code);
    if (!name) {
        return std::nullopt;
    }
    KeyCombo c;
    c.shift = (mods & KMOD_SHIFT) != 0;
    c.ctrl = (mods & KMOD_CTRL) != 0;
    c.alt = (mods & KMOD_ALT) != 0;
    if (*name == "LeftShift" || *name == "RightShift") {
        c.shift = false;
    } else if (*name == "LeftCtrl" || *name == "RightCtrl") {
        c.ctrl = false;
    } else if (*name == "LeftAlt" || *name == "RightAlt") {
        c.alt = false;
    }
    c.key = std::move(*name);
    return c;
}

}  // namespace trainz::input
