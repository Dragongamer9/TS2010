#include "input/ActionMap.h"

namespace trainz::input {

ActionMap::ActionMap(const KeyBindings& bindings) {
    for (const auto& [group, actions] : bindings.groups) {
        auto& byCombo = index_[group];
        for (const auto& action : actions) {
            for (const auto& combo : action.combos) {
                byCombo[describe(combo)].push_back(action.name);
            }
        }
    }
}

std::vector<ActionHit> ActionMap::actionsFor(const std::vector<std::string>& activeGroups,
                                             const KeyCombo& press) const {
    std::vector<ActionHit> hits;
    const std::string key = describe(press);
    for (const auto& group : activeGroups) {
        const auto g = index_.find(group);
        if (g == index_.end()) {
            continue;
        }
        const auto c = g->second.find(key);
        if (c == g->second.end()) {
            continue;
        }
        for (const auto& action : c->second) {
            hits.push_back({group, action});
        }
    }
    return hits;
}

std::vector<SharedCombo> ActionMap::sharedCombos() const {
    std::vector<SharedCombo> out;
    for (const auto& [group, byCombo] : index_) {
        for (const auto& [combo, actions] : byCombo) {
            if (actions.size() > 1) {
                out.push_back({group, combo, actions});
            }
        }
    }
    return out;
}

}  // namespace trainz::input
