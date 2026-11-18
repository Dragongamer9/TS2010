#include "input/ActionMap.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace trainz::input;

namespace {

KeyBindings parse(const std::string& text) {
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    return parseKeyBindings(bytes);
}

KeyCombo press(std::string key, bool shift = false, bool ctrl = false, bool alt = false) {
    KeyCombo c;
    c.key = std::move(key);
    c.shift = shift;
    c.ctrl = ctrl;
    c.alt = alt;
    return c;
}

const char* kText =
    "driver\n{\n"
    "  throttle_up    w, KP8\n"
    "  aws_isolate    alt 1\n"
    "  view_cab       1\n"
    "  dcc_up         w\n"
    "  mod_ctrl       leftcontrolkey, rightcontrolkey\n"
    "}\n"
    "gameplay\n{\n"
    "  quick_save     ctrl s\n"
    "  pause_game     w\n"
    "}\n";

}  // namespace

TEST(ActionMap, ExactModifiersOnly) {
    const ActionMap map(parse(kText));
    const std::vector<std::string> driver = {"driver"};

    const auto plain = map.actionsFor(driver, press("1"));
    ASSERT_EQ(plain.size(), 1U);
    EXPECT_EQ(plain[0].action, "view_cab");

    const auto alt = map.actionsFor(driver, press("1", false, false, true));
    ASSERT_EQ(alt.size(), 1U);
    EXPECT_EQ(alt[0].action, "aws_isolate");

    EXPECT_TRUE(map.actionsFor(driver, press("1", true)).empty());
    EXPECT_TRUE(map.actionsFor(driver, press("Q")).empty());
}

TEST(ActionMap, SeveralActionsAndGroupsInOrder) {
    const ActionMap map(parse(kText));
    const auto hits = map.actionsFor({"gameplay", "driver"}, press("W"));
    ASSERT_EQ(hits.size(), 3U);
    EXPECT_EQ(hits[0].group, "gameplay");
    EXPECT_EQ(hits[0].action, "pause_game");
    EXPECT_EQ(hits[1].action, "throttle_up");
    EXPECT_EQ(hits[2].action, "dcc_up");

    EXPECT_TRUE(map.actionsFor({"menu"}, press("W")).empty());
    EXPECT_EQ(map.actionsFor({"driver"}, press("KP8")).at(0).action, "throttle_up");
    EXPECT_EQ(map.actionsFor({"gameplay"}, press("S", false, true)).at(0).action, "quick_save");
    EXPECT_EQ(map.actionsFor({"driver"}, press("LeftCtrl")).at(0).action, "mod_ctrl");
}

TEST(ActionMap, ReportsSharedCombos) {
    const ActionMap map(parse(kText));
    const auto c = map.sharedCombos();
    ASSERT_EQ(c.size(), 1U);
    EXPECT_EQ(c[0].group, "driver");
    EXPECT_EQ(c[0].combo, "W");
    EXPECT_EQ(c[0].actions, (std::vector<std::string>{"throttle_up", "dcc_up"}));
}

TEST(ActionMap, CanonicalListIsConsistent) {
    for (const auto& k : allCanonicalKeys()) {
        bool named = false;
        for (const auto& n : gameKeyNames()) {
            named = named || (k == n.canonical && canonicalKey(n.name) == k);
        }
        EXPECT_TRUE(named) << k;
    }
    EXPECT_TRUE(isModifierKey("LeftShift"));
    EXPECT_FALSE(isModifierKey("A"));
}

