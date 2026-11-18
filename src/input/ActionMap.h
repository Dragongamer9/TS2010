// Looks up which actions a key press triggers, from parsed keyboard.txt bindings.
// No SDL here: a press is a KeyCombo (see SdlKeys.h for turning SDL events into one).
//
// Matching rules (the port's own; the game's dispatch code is not traced):
// - modifiers must match exactly: shift+W does not trigger an action bound to W;
// - left and right Shift/Ctrl/Alt count the same as modifiers;
// - when several groups are active, results come in the order the groups are given;
// - every action bound to the combo is returned; choosing by mode is the caller's job.
#pragma once

#include "input/KeyBindings.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace trainz::input {

struct ActionHit {
    std::string group;
    std::string action;
};

// Two or more actions in one group bound to the same combo. This is normal in the
// shipped file (W is cab throttle, DCC up and regulator up; Surveyor tools share
// letters). The action names suggest the game picks one by mode (cab vs DCC, the
// current tool), but that is an inference: the game's dispatch is not traced.
struct SharedCombo {
    std::string group;
    std::string combo;  // describe() form
    std::vector<std::string> actions;
};

class ActionMap {
public:
    explicit ActionMap(const KeyBindings& bindings);

    std::vector<ActionHit> actionsFor(const std::vector<std::string>& activeGroups, const KeyCombo& press) const;

    std::vector<SharedCombo> sharedCombos() const;

private:
    // group -> describe(combo) -> actions in file order
    std::map<std::string, std::map<std::string, std::vector<std::string>>> index_;
};

}  // namespace trainz::input
