// GamepadSlot with SDL's virtual game controllers (no hardware needed).
#include "input/SdlGamepad.h"

#include <gtest/gtest.h>

namespace {

class VirtualPads : public ::testing::Test {
protected:
    void SetUp() override {
        if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
            GTEST_SKIP() << "SDL game controllers unavailable: " << SDL_GetError();
        }
    }
    void TearDown() override {
        while (SDL_NumJoysticks() > 0 && SDL_JoystickIsVirtual(0)) {
            SDL_JoystickDetachVirtual(0);
        }
        SDL_Quit();
    }
    // A virtual gamepad: its device index, or -1.
    static int attach() {
        return SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    }
    static int indexOf(SDL_JoystickID id) {
        for (int j = 0; j < SDL_NumJoysticks(); ++j) {
            if (SDL_JoystickGetDeviceInstanceID(j) == id) {
                return j;
            }
        }
        return -1;
    }
};

}  // namespace

TEST_F(VirtualPads, NoneThenFirstThenHandOverOnUnplug) {
    trainz::input::GamepadSlot slot;
    EXPECT_FALSE(slot.openIfNone());  // nothing plugged in
    EXPECT_FALSE(slot.isOurs(0));

    const int a = attach(), b = attach();
    ASSERT_GE(a, 0) << SDL_GetError();
    ASSERT_GE(b, 0) << SDL_GetError();
    ASSERT_TRUE(SDL_IsGameController(a) && SDL_IsGameController(b));
    const SDL_JoystickID idA = SDL_JoystickGetDeviceInstanceID(a), idB = SDL_JoystickGetDeviceInstanceID(b);

    ASSERT_TRUE(slot.openIfNone());  // the first one
    EXPECT_TRUE(slot.isOurs(idA));
    EXPECT_FALSE(slot.isOurs(idB));  // e.g. Start on the other pad: not ours

    slot.removed(idB);  // the other one unplugged: nothing changes
    EXPECT_TRUE(slot.isOurs(idA));

    ASSERT_EQ(SDL_JoystickDetachVirtual(indexOf(idA)), 0) << SDL_GetError();  // ours unplugged...
    slot.removed(idA);
    ASSERT_NE(slot.get(), nullptr);  // ...the one still plugged in takes over
    EXPECT_TRUE(slot.isOurs(idB));

    ASSERT_EQ(SDL_JoystickDetachVirtual(indexOf(idB)), 0) << SDL_GetError();
    slot.removed(idB);
    EXPECT_EQ(slot.get(), nullptr);  // none left
    EXPECT_FALSE(slot.isOurs(idB));
}
