#include "input/SdlKeys.h"

#include <gtest/gtest.h>

#include <set>

using namespace trainz::input;

TEST(SdlKeys, EveryCanonicalKeyRoundTrips) {
    std::set<int> seen;
    for (const auto& k : allCanonicalKeys()) {
        const auto code = scancodeFor(k);
        ASSERT_TRUE(code.has_value()) << k;
        EXPECT_EQ(canonicalFor(*code), k) << k;
        EXPECT_TRUE(seen.insert(*code).second) << "two names for one scancode: " << k;
    }
}

TEST(SdlKeys, KnownPositions) {
    EXPECT_EQ(scancodeFor("A"), SDL_SCANCODE_A);
    EXPECT_EQ(scancodeFor("0"), SDL_SCANCODE_0);
    EXPECT_EQ(scancodeFor("9"), SDL_SCANCODE_9);
    EXPECT_EQ(scancodeFor("F12"), SDL_SCANCODE_F12);
    EXPECT_EQ(scancodeFor("F15"), SDL_SCANCODE_F15);
    EXPECT_EQ(scancodeFor("KP9"), SDL_SCANCODE_KP_9);
    EXPECT_EQ(scancodeFor("Equals"), SDL_SCANCODE_EQUALS);
    EXPECT_EQ(scancodeFor("nope"), std::nullopt);
    EXPECT_EQ(canonicalFor(SDL_SCANCODE_VOLUMEUP), std::nullopt);
}

TEST(SdlKeys, ComboFromKey) {
    auto c = comboFromKey(SDL_SCANCODE_1, KMOD_LALT);
    ASSERT_TRUE(c);
    EXPECT_EQ(describe(*c), "alt+1");

    c = comboFromKey(SDL_SCANCODE_S, KMOD_RCTRL | KMOD_LSHIFT | KMOD_NUM);
    ASSERT_TRUE(c);
    EXPECT_EQ(describe(*c), "ctrl+shift+S");  // NumLock is not a modifier

    c = comboFromKey(SDL_SCANCODE_LCTRL, KMOD_LCTRL);
    ASSERT_TRUE(c);
    EXPECT_EQ(describe(*c), "LeftCtrl");  // the key itself, without its own flag

    EXPECT_FALSE(comboFromKey(SDL_SCANCODE_VOLUMEUP, 0));
}
