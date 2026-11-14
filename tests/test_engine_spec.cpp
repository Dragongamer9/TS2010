#include "content/EngineSpec.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace {

trainz::content::ConfigNode motorWithCounts(trainz::content::ConfigNode value) {
    using namespace trainz::content;
    ConfigNode root;
    root.type = ConfigType::Group;
    ConfigNode motor;
    motor.key = "motor";
    motor.type = ConfigType::Group;
    value.key = "axle-count";
    motor.children.push_back(value);
    value.key = "throttle-notches";
    motor.children.push_back(value);
    root.children.push_back(motor);
    return root;
}

}  // namespace

TEST(EngineSpec, RejectsInvalidMotorCountsBeforeIntegerConversion) {
    using namespace trainz::content;
    // The float immediately above the signed-int range also catches INT_MAX rounded
    // up to 2^31 when authored as a config float, rather than an integer.
    for (const float value : {-1.0F, -0.5F, 1.5F, 2147483648.0F, 1e30F,
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
        SCOPED_TRACE(value);
        ConfigNode node;
        node.type = ConfigType::Floats;
        node.floats.push_back(value);
        const auto spec = parseEngineSpec(motorWithCounts(node));
        EXPECT_FALSE(spec.axleCount.has_value());
        EXPECT_FALSE(spec.throttleNotches.has_value());
    }
    ConfigNode negative;
    negative.type = ConfigType::Integers;
    negative.ints.push_back(-1);
    const auto spec = parseEngineSpec(motorWithCounts(negative));
    EXPECT_FALSE(spec.axleCount.has_value());
    EXPECT_FALSE(spec.throttleNotches.has_value());
}

TEST(EngineSpec, PreservesValidMotorCountsIncludingBoundaries) {
    using namespace trainz::content;
    for (const std::int32_t value : {0, 6, std::numeric_limits<std::int32_t>::max()}) {
        SCOPED_TRACE(value);
        ConfigNode node;
        node.type = ConfigType::Integers;
        node.ints.push_back(value);
        const auto spec = parseEngineSpec(motorWithCounts(node));
        ASSERT_TRUE(spec.axleCount.has_value());
        ASSERT_TRUE(spec.throttleNotches.has_value());
        EXPECT_EQ(*spec.axleCount, value);
        EXPECT_EQ(*spec.throttleNotches, value);
    }
    for (const float value : {0.0F, 8.0F, 2147483520.0F}) {
        SCOPED_TRACE(value);
        ConfigNode node;
        node.type = ConfigType::Floats;
        node.floats.push_back(value);
        const auto spec = parseEngineSpec(motorWithCounts(node));
        ASSERT_TRUE(spec.axleCount.has_value());
        ASSERT_TRUE(spec.throttleNotches.has_value());
        EXPECT_EQ(*spec.axleCount, static_cast<int>(value));
        EXPECT_EQ(*spec.throttleNotches, static_cast<int>(value));
    }
}
