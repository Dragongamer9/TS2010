#include "input/KeyBindings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace trainz::input;

namespace {

KeyBindings parse(const std::string& text) {
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    return parseKeyBindings(bytes);
}

}  // namespace

TEST(KeyBindings, CanonicalNamesAndAliases) {
    EXPECT_EQ(canonicalKey("a"), "A");
    EXPECT_EQ(canonicalKey("Z"), "Z");
    EXPECT_EQ(canonicalKey("7"), "7");
    EXPECT_EQ(canonicalKey("f2"), "F2");
    EXPECT_EQ(canonicalKey("F12"), "F12");
    EXPECT_EQ(canonicalKey("F13"), std::nullopt);  // the game names F1-F12 only
    EXPECT_EQ(canonicalKey("-"), "Minus");
    EXPECT_EQ(canonicalKey("\\"), "Backslash");
    // Names that are in the game's display table but not in its parser:
    for (const char* n : {"page up", "kp/", "kpdel", "back", "~", ".", ",", "caps lock", "left alt"}) {
        EXPECT_EQ(canonicalKey(n), std::nullopt) << n;
    }
    EXPECT_EQ(canonicalKey("pgup"), "PageUp");
    EXPECT_EQ(canonicalKey("PageUp"), "PageUp");
    EXPECT_EQ(canonicalKey("KPPgUp"), "KP9");
    EXPECT_EQ(canonicalKey("kpdn"), "KP2");
    EXPECT_EQ(canonicalKey("fullstop"), "Period");
    EXPECT_EQ(canonicalKey("'"), "Apostrophe");
    EXPECT_EQ(canonicalKey("plus"), "Equals");
    EXPECT_EQ(canonicalKey("escape"), "Esc");
    EXPECT_EQ(canonicalKey(""), std::nullopt);
    EXPECT_EQ(canonicalKey("warpdrive"), std::nullopt);
}

TEST(KeyBindings, TracedTableShape) {
    EXPECT_EQ(gameKeyNames().size(), 141U);
    EXPECT_EQ(allCanonicalKeys().size(), 98U);
    EXPECT_EQ(engineKeyCode("Esc"), 0x01);
    EXPECT_EQ(engineKeyCode("1"), 0x03);
    EXPECT_EQ(engineKeyCode("Q"), 0x11);
    EXPECT_EQ(engineKeyCode("LeftShift"), 0x2b);
    EXPECT_EQ(engineKeyCode("KPSlash"), 0x5b);
    EXPECT_EQ(engineKeyCode("PageUp"), 0x60);
    EXPECT_EQ(engineKeyCode("Delete"), 0x67);
    EXPECT_EQ(engineKeyCode("nope"), std::nullopt);
    // Every canonical key has a game name and exactly one engine code.
    std::vector<int> codes;
    for (const auto& k : allCanonicalKeys()) {
        codes.push_back(*engineKeyCode(k));
    }
    std::sort(codes.begin(), codes.end());
    EXPECT_EQ(std::adjacent_find(codes.begin(), codes.end()), codes.end());
}

TEST(KeyBindings, WordsApplyInOrderLikeTheGame) {
    std::vector<std::string> w;
    auto one = [&](std::string_view text) {
        const auto c = parseComboList(text, "t", w);
        return c.size() == 1 ? describe(c[0]) : std::string("<") + std::to_string(c.size()) + ">";
    };
    EXPECT_EQ(one("b ctrl"), "ctrl+B");            // modifier after the key still counts
    EXPECT_EQ(one("ctrl null b"), "B");            // null clears key and modifiers
    EXPECT_EQ(one("ctrl warpdrive b"), "B");       // so does an unknown word
    EXPECT_EQ(one("ctrl b warpdrive"), "<0>");     // ... even after the key
    EXPECT_EQ(one("CTRL  B"), "ctrl+B");           // case and double spaces
    EXPECT_EQ(one("ctrl\tb"), "<0>");              // a tab is part of the word
    EXPECT_EQ(one("ctrl"), "<0>");                 // modifiers alone bind nothing
    EXPECT_EQ(one("a, a"), "A");                   // repeats kept once
    EXPECT_EQ(parseComboList("", "t", w).size(), 0U);
    EXPECT_EQ(parseComboList(",,", "t", w).size(), 0U);
}

TEST(KeyBindings, ParsesGroupsCombosAndNull) {
    const auto kb = parse(
        "\r\ndriver\r\n{\r\n"
        "  train_cabin_brake_release       q, KP9, KPPgUp\r\n"
        "  train_cabin_aws_isolate\t\talt 1\r\n"
        "  train_cabin_brake_1             null\r\n"
        "  cmd_get_off_train\r\n"
        "  quick_save                      ctrl shift F5\r\n"
        "}\r\n"
        "menu\r\n{\r\n  menu_quit  esc\r\n}\r\n");
    ASSERT_EQ(kb.groups.size(), 2U);

    const auto* release = kb.find("driver", "train_cabin_brake_release");
    ASSERT_NE(release, nullptr);
    ASSERT_EQ(release->combos.size(), 2U);  // KP9 and KPPgUp are the same key
    EXPECT_EQ(release->combos[0].key, "Q");
    EXPECT_EQ(release->combos[1].key, "KP9");

    const auto* aws = kb.find("driver", "train_cabin_aws_isolate");
    ASSERT_NE(aws, nullptr);
    ASSERT_EQ(aws->combos.size(), 1U);
    EXPECT_TRUE(aws->combos[0].alt);
    EXPECT_EQ(aws->combos[0].key, "1");

    EXPECT_TRUE(kb.find("driver", "train_cabin_brake_1")->combos.empty());
    EXPECT_TRUE(kb.find("driver", "cmd_get_off_train")->combos.empty());

    const auto* save = kb.find("driver", "quick_save");
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(describe(save->combos.at(0)), "ctrl+shift+F5");

    EXPECT_EQ(kb.find("menu", "menu_quit")->combos.at(0).key, "Esc");
    EXPECT_EQ(kb.find("menu", "nope"), nullptr);
    EXPECT_EQ(kb.find("nope", "menu_quit"), nullptr);
    EXPECT_TRUE(kb.warnings.empty());
}

TEST(KeyBindings, LoneDigitValue) {
    const auto kb = parse("driver\n{\n  view_1  1\n}\n");
    const auto* a = kb.find("driver", "view_1");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->combos.size(), 1U);
    EXPECT_EQ(a->combos[0].key, "1");
}

TEST(KeyBindings, OddLinesWarnInsteadOfFailing) {
    // Line 116 of the shipped file. The game applies every word in turn, so each
    // letter replaces the one before: the result is ctrl+alt+shift+Z (traced).
    const auto kb = parse(
        "driver\n{\n"
        "  camera_car  alt ctrl shift c a r z\n"
        "  bogus       ctrl warpdrive, b\n"
        "}\n");
    const auto* car = kb.find("driver", "camera_car");
    ASSERT_NE(car, nullptr);
    ASSERT_EQ(car->combos.size(), 1U);
    EXPECT_EQ(describe(car->combos[0]), "ctrl+alt+shift+Z");

    const auto* bogus = kb.find("driver", "bogus");
    ASSERT_NE(bogus, nullptr);
    ASSERT_EQ(bogus->combos.size(), 1U);  // the bad combo is skipped, "b" is kept
    EXPECT_EQ(bogus->combos[0].key, "B");
    EXPECT_EQ(kb.warnings.size(), 4U);  // c, a, r, warpdrive
}

TEST(KeyBindings, GarbageNeverCrashes) {
    for (const std::string& text : {std::string(""), std::string("{"), std::string("}}}"),
                                   std::string("x\n{\n a ,,,, , \n}\n"), std::string("\0\xff\x01", 3)}) {
        try {
            (void)parse(text);
        } catch (const std::exception&) {
            // a clean error is fine; a crash is not
        }
    }
}
