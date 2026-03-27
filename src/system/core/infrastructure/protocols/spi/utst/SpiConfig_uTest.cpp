/**
 * @file SpiConfig_uTest.cpp
 * @brief Unit tests for SPI configuration types.
 */

#include "src/system/core/infrastructure/protocols/spi/inc/SpiConfig.hpp"

#include <gtest/gtest.h>

using apex::protocols::spi::BitOrder;
using apex::protocols::spi::SpiConfig;
using apex::protocols::spi::SpiMode;
using apex::protocols::spi::toString;

/* ----------------------------- SpiMode Tests ----------------------------- */

/** @test Verify SpiMode values match expected CPOL/CPHA combinations. */
TEST(SpiConfig, ModeValues) {
  EXPECT_EQ(static_cast<int>(SpiMode::MODE_0), 0);
  EXPECT_EQ(static_cast<int>(SpiMode::MODE_1), 1);
  EXPECT_EQ(static_cast<int>(SpiMode::MODE_2), 2);
  EXPECT_EQ(static_cast<int>(SpiMode::MODE_3), 3);
}

/** @test Verify toString for SpiMode. */
TEST(SpiConfig, ModeToString) {
  EXPECT_STREQ(toString(SpiMode::MODE_0), "MODE_0");
  EXPECT_STREQ(toString(SpiMode::MODE_1), "MODE_1");
  EXPECT_STREQ(toString(SpiMode::MODE_2), "MODE_2");
  EXPECT_STREQ(toString(SpiMode::MODE_3), "MODE_3");
}

/* ----------------------------- BitOrder Tests ----------------------------- */

/** @test Verify BitOrder values. */
TEST(SpiConfig, BitOrderValues) {
  EXPECT_EQ(static_cast<int>(BitOrder::MSB_FIRST), 0);
  EXPECT_EQ(static_cast<int>(BitOrder::LSB_FIRST), 1);
}

/** @test Verify toString for BitOrder. */
TEST(SpiConfig, BitOrderToString) {
  EXPECT_STREQ(toString(BitOrder::MSB_FIRST), "MSB_FIRST");
  EXPECT_STREQ(toString(BitOrder::LSB_FIRST), "LSB_FIRST");
}

/* ----------------------------- SpiConfig Tests ----------------------------- */

/** @test Verify default SpiConfig values. */
TEST(SpiConfig, DefaultValues) {
  SpiConfig config;

  EXPECT_EQ(config.mode, SpiMode::MODE_0);
  EXPECT_EQ(config.bitOrder, BitOrder::MSB_FIRST);
  EXPECT_EQ(config.bitsPerWord, 8);
  EXPECT_EQ(config.maxSpeedHz, 1000000U);
  EXPECT_FALSE(config.csHigh);
  EXPECT_FALSE(config.threeWire);
  EXPECT_FALSE(config.loopback);
  EXPECT_FALSE(config.noCs);
  EXPECT_FALSE(config.ready);
}

/** @test Verify CPOL extraction from mode. */
TEST(SpiConfig, CpolExtraction) {
  SpiConfig config;

  config.mode = SpiMode::MODE_0;
  EXPECT_FALSE(config.cpol());

  config.mode = SpiMode::MODE_1;
  EXPECT_FALSE(config.cpol());

  config.mode = SpiMode::MODE_2;
  EXPECT_TRUE(config.cpol());

  config.mode = SpiMode::MODE_3;
  EXPECT_TRUE(config.cpol());
}

/** @test Verify CPHA extraction from mode. */
TEST(SpiConfig, CphaExtraction) {
  SpiConfig config;

  config.mode = SpiMode::MODE_0;
  EXPECT_FALSE(config.cpha());

  config.mode = SpiMode::MODE_1;
  EXPECT_TRUE(config.cpha());

  config.mode = SpiMode::MODE_2;
  EXPECT_FALSE(config.cpha());

  config.mode = SpiMode::MODE_3;
  EXPECT_TRUE(config.cpha());
}

/** @test Verify speedMHz calculation. */
TEST(SpiConfig, SpeedMhzCalculation) {
  SpiConfig config;

  config.maxSpeedHz = 1000000;
  EXPECT_DOUBLE_EQ(config.speedMHz(), 1.0);

  config.maxSpeedHz = 10000000;
  EXPECT_DOUBLE_EQ(config.speedMHz(), 10.0);

  config.maxSpeedHz = 500000;
  EXPECT_DOUBLE_EQ(config.speedMHz(), 0.5);
}

/** @test Verify SpiConfig is POD-like and can be copied. */
TEST(SpiConfig, Copyable) {
  SpiConfig config1;
  config1.mode = SpiMode::MODE_3;
  config1.maxSpeedHz = 5000000;
  config1.loopback = true;

  SpiConfig config2 = config1;

  EXPECT_EQ(config2.mode, SpiMode::MODE_3);
  EXPECT_EQ(config2.maxSpeedHz, 5000000U);
  EXPECT_TRUE(config2.loopback);
}
