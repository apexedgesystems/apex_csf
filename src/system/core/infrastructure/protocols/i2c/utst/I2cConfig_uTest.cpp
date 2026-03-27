/**
 * @file I2cConfig_uTest.cpp
 * @brief Unit tests for I2C configuration types.
 */

#include "src/system/core/infrastructure/protocols/i2c/inc/I2cConfig.hpp"

#include <gtest/gtest.h>

using apex::protocols::i2c::AddressMode;
using apex::protocols::i2c::I2cConfig;
using apex::protocols::i2c::toString;

/* ----------------------------- AddressMode Tests ----------------------------- */

/** @test Verify AddressMode values. */
TEST(I2cConfig, AddressModeValues) {
  EXPECT_EQ(static_cast<int>(AddressMode::SEVEN_BIT), 0);
  EXPECT_EQ(static_cast<int>(AddressMode::TEN_BIT), 1);
}

/** @test Verify toString for AddressMode. */
TEST(I2cConfig, AddressModeToString) {
  EXPECT_STREQ(toString(AddressMode::SEVEN_BIT), "SEVEN_BIT");
  EXPECT_STREQ(toString(AddressMode::TEN_BIT), "TEN_BIT");
}

/* ----------------------------- I2cConfig Tests ----------------------------- */

/** @test Verify default I2cConfig values. */
TEST(I2cConfig, DefaultValues) {
  I2cConfig config;

  EXPECT_EQ(config.addressMode, AddressMode::SEVEN_BIT);
  EXPECT_FALSE(config.enablePec);
  EXPECT_FALSE(config.forceAccess);
  EXPECT_EQ(config.retryCount, 0);
}

/** @test Verify I2cConfig is POD-like and can be copied. */
TEST(I2cConfig, Copyable) {
  I2cConfig config1;
  config1.addressMode = AddressMode::TEN_BIT;
  config1.enablePec = true;
  config1.forceAccess = true;
  config1.retryCount = 3;

  I2cConfig config2 = config1;

  EXPECT_EQ(config2.addressMode, AddressMode::TEN_BIT);
  EXPECT_TRUE(config2.enablePec);
  EXPECT_TRUE(config2.forceAccess);
  EXPECT_EQ(config2.retryCount, 3);
}

/** @test Verify config can be modified. */
TEST(I2cConfig, Modifiable) {
  I2cConfig config;

  config.addressMode = AddressMode::TEN_BIT;
  EXPECT_EQ(config.addressMode, AddressMode::TEN_BIT);

  config.enablePec = true;
  EXPECT_TRUE(config.enablePec);

  config.retryCount = 5;
  EXPECT_EQ(config.retryCount, 5);
}
