#include "SignalAsset.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace trainz::content {

namespace {
std::optional<int> numberKey(const std::string& k) {
    if (k.empty() || k.size() > 4 || !std::all_of(k.begin(), k.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::nullopt;
    }
    return std::stoi(k);
}
}  // namespace

SignalLook parseSignalLook(const ConfigNode& root) {
    SignalLook look;
    if (const ConfigNode* s = root.child("signals"); s != nullptr && s->type == ConfigType::Group) {
        for (const auto& st : s->children) {
            const auto n = numberKey(st.key);
            if (!n || st.type != ConfigType::Group) {
                continue;
            }
            std::vector<int> lit;
            if (const ConfigNode* l = st.child("light")) {
                for (const auto v : l->ints) {
                    lit.push_back(v);
                }
                for (const auto v : l->floats) {
                    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1000) {
                        lit.push_back(static_cast<int>(v));
                    }
                }
                if (lit.empty() && !l->text.empty()) {  // e.g. "1 3" read as text
                    std::size_t i = 0;
                    while (i < l->text.size()) {
                        while (i < l->text.size() && (l->text[i] == ' ' || l->text[i] == ',')) {
                            ++i;
                        }
                        std::size_t j = i;
                        if (j < l->text.size() && l->text[j] == '-') {
                            ++j;
                        }
                        while (j < l->text.size() && std::isdigit(static_cast<unsigned char>(l->text[j])) != 0) {
                            ++j;
                        }
                        if (j == i || j - i > 4) {
                            break;
                        }
                        lit.push_back(std::stoi(l->text.substr(i, j - i)));
                        i = j;
                    }
                }
            }
            look.states[*n] = lit;
        }
    }
    if (const ConfigNode* l = root.child("lights"); l != nullptr && l->type == ConfigType::Group) {
        for (const auto& li : l->children) {
            const auto n = numberKey(li.key);
            if (!n || li.type != ConfigType::Group) {
                continue;
            }
            if (const ConfigNode* c = li.child("corona"); c != nullptr && !c->text.empty()) {
                look.coronas[*n] = c->text;
            }
        }
    }
    return look;
}

std::optional<int> stateForAspect(const SignalLook& look, int aspect) {
    if (look.states.empty()) {
        return std::nullopt;
    }
    auto inRange = [&](int lo, int hi) -> std::optional<int> {
        const auto it = look.states.lower_bound(lo);
        return it != look.states.end() && it->first <= hi ? std::optional<int>(it->first) : std::nullopt;
    };
    const auto stop = inRange(0, 3);
    const auto caution = inRange(4, 7);
    const auto clear = look.states.count(8) != 0 ? std::optional<int>(8) : inRange(9, 9999);
    if (aspect <= 0) {
        return stop ? stop : look.states.begin()->first;
    }
    if (aspect == 1) {
        return caution ? caution : stop ? stop : look.states.begin()->first;
    }
    return clear ? clear : caution ? caution : stop ? stop : look.states.rbegin()->first;
}

std::array<std::uint8_t, 3> coronaColour(const std::string& name) {
    std::string n(name);
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
    if (has("red")) {
        return {255, 40, 30};
    }
    if (has("yellow") || has("amber") || has("orange")) {
        return {255, 190, 30};
    }
    if (has("green")) {
        return {60, 255, 90};
    }
    if (has("blue")) {
        return {80, 140, 255};
    }
    return {255, 250, 235};  // lunar, white or unknown
}

}  // namespace trainz::content
