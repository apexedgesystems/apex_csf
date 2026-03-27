/**
 * @file Stm32I2c_uTest.cpp
 * @brief Unit tests for Stm32I2c implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_STM32_MOCK,
 * which removes STM32 HAL dependencies and allows testing the
 * interface compliance and stats tracking logic.
 */

#define APEX_HAL_STM32_MOCK 1

#include "src/system/core/hal/stm32/inc/Stm32I2c.hpp"

#include <gtest/gtest.h>

using apex::hal::I2cAddressMode;
using apex::hal::I2cConfig;
using apex::hal::I2cSpeed;
using apex::hal::I2cStats;
using apex::hal::I2cStatus;
using apex::hal::stm32::Stm32I2c;
using apex::hal::stm32::Stm32I2cOptions;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify Stm32I2c can be default constructed in mock mode. */
TEST(Stm32I2c, DefaultConstruction) {
  Stm32I2c i2c;

  EXPECT_FALSE(i2c.isInitialized());
  EXPECT_FALSE(i2c.isBusy());
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(Stm32I2c, InitSucceeds) {
  Stm32I2c i2c;
  I2cConfig config;
  config.speed = I2cSpeed::FAST;

  const I2cStatus STATUS = i2c.init(config);

  EXPECT_EQ(STATUS, I2cStatus::OK);
  EXPECT_TRUE(i2c.isInitialized());
}

/** @test Verify init with all speed modes succeeds. */
TEST(Stm32I2c, InitAllSpeeds) {
  for (int s = 0; s <= 2; ++s) {
    Stm32I2c i2c;
    I2cConfig config;
    config.speed = static_cast<I2cSpeed>(s);

    const I2cStatus STATUS = i2c.init(config);

    EXPECT_EQ(STATUS, I2cStatus::OK);
    i2c.deinit();
  }
}

/** @test Verify deinit resets state. */
TEST(Stm32I2c, DeinitResetsState) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  i2c.deinit();

  EXPECT_FALSE(i2c.isInitialized());
}

/** @test Verify multiple init/deinit cycles work. */
TEST(Stm32I2c, MultipleInitDeinitCycles) {
  Stm32I2c i2c;
  I2cConfig config;

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(i2c.init(config), I2cStatus::OK);
    EXPECT_TRUE(i2c.isInitialized());
    i2c.deinit();
    EXPECT_FALSE(i2c.isInitialized());
  }
}

/** @test Verify double init reinitializes cleanly (double-init guard). */
TEST(Stm32I2c, DoubleInitReinitializes) {
  Stm32I2c i2c;
  I2cConfig config;

  EXPECT_EQ(i2c.init(config), I2cStatus::OK);
  EXPECT_TRUE(i2c.isInitialized());

  // Init again without explicit deinit -- should succeed
  config.speed = I2cSpeed::FAST;
  EXPECT_EQ(i2c.init(config), I2cStatus::OK);
  EXPECT_TRUE(i2c.isInitialized());
}

/* ----------------------------- Write Tests ----------------------------- */

/** @test Verify write returns ERROR_NOT_INIT when not initialized. */
TEST(Stm32I2c, WriteNotInitialized) {
  Stm32I2c i2c;
  uint8_t data[4] = {0x01, 0x02, 0x03, 0x04};

  const I2cStatus STATUS = i2c.write(0x50, data, 4);

  EXPECT_EQ(STATUS, I2cStatus::ERROR_NOT_INIT);
}

/** @test Verify write succeeds in mock mode. */
TEST(Stm32I2c, WriteSucceeds) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t data[4] = {0xAB, 0xCD, 0xEF, 0x01};

  const I2cStatus STATUS = i2c.write(0x50, data, 4);

  EXPECT_EQ(STATUS, I2cStatus::OK);
}

/** @test Verify write rejects null pointer. */
TEST(Stm32I2c, WriteNullPointer) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  const I2cStatus STATUS = i2c.write(0x50, nullptr, 4);

  EXPECT_EQ(STATUS, I2cStatus::ERROR_INVALID_ARG);
}

/** @test Verify write rejects zero length. */
TEST(Stm32I2c, WriteZeroLength) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t data[1] = {0x01};

  const I2cStatus STATUS = i2c.write(0x50, data, 0);

  EXPECT_EQ(STATUS, I2cStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Read Tests ----------------------------- */

/** @test Verify read returns ERROR_NOT_INIT when not initialized. */
TEST(Stm32I2c, ReadNotInitialized) {
  Stm32I2c i2c;
  uint8_t data[4] = {};

  const I2cStatus STATUS = i2c.read(0x50, data, 4);

  EXPECT_EQ(STATUS, I2cStatus::ERROR_NOT_INIT);
}

/** @test Verify read succeeds in mock mode. */
TEST(Stm32I2c, ReadSucceeds) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t data[4] = {};

  const I2cStatus STATUS = i2c.read(0x50, data, 4);

  EXPECT_EQ(STATUS, I2cStatus::OK);
  // Mock fills with 0xFF (no slave responding)
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(data[i], 0xFF);
  }
}

/** @test Verify read rejects null pointer. */
TEST(Stm32I2c, ReadNullPointer) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  const I2cStatus STATUS = i2c.read(0x50, nullptr, 4);

  EXPECT_EQ(STATUS, I2cStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects zero length. */
TEST(Stm32I2c, ReadZeroLength) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t data[1] = {};

  const I2cStatus STATUS = i2c.read(0x50, data, 0);

  EXPECT_EQ(STATUS, I2cStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- WriteRead Tests ----------------------------- */

/** @test Verify writeRead returns ERROR_NOT_INIT when not initialized. */
TEST(Stm32I2c, WriteReadNotInitialized) {
  Stm32I2c i2c;
  uint8_t tx[1] = {0x75};
  uint8_t rx[1] = {};

  const I2cStatus STATUS = i2c.writeRead(0x68, tx, 1, rx, 1);

  EXPECT_EQ(STATUS, I2cStatus::ERROR_NOT_INIT);
}

/** @test Verify writeRead succeeds in mock mode. */
TEST(Stm32I2c, WriteReadSucceeds) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t tx[1] = {0x75}; // Register address
  uint8_t rx[4] = {};

  const I2cStatus STATUS = i2c.writeRead(0x68, tx, 1, rx, 4);

  EXPECT_EQ(STATUS, I2cStatus::OK);
  // Mock fills rxData with incrementing pattern
  EXPECT_EQ(rx[0], 0x00);
  EXPECT_EQ(rx[1], 0x01);
  EXPECT_EQ(rx[2], 0x02);
  EXPECT_EQ(rx[3], 0x03);
}

/** @test Verify writeRead rejects null txData. */
TEST(Stm32I2c, WriteReadNullTx) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t rx[4] = {};

  const I2cStatus STATUS = i2c.writeRead(0x68, nullptr, 1, rx, 4);

  EXPECT_EQ(STATUS, I2cStatus::ERROR_INVALID_ARG);
}

/** @test Verify writeRead rejects null rxData. */
TEST(Stm32I2c, WriteReadNullRx) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t tx[1] = {0x75};

  const I2cStatus STATUS = i2c.writeRead(0x68, tx, 1, nullptr, 4);

  EXPECT_EQ(STATUS, I2cStatus::ERROR_INVALID_ARG);
}

/** @test Verify writeRead rejects zero txLen. */
TEST(Stm32I2c, WriteReadZeroTxLen) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t tx[1] = {0x75};
  uint8_t rx[4] = {};

  const I2cStatus STATUS = i2c.writeRead(0x68, tx, 0, rx, 4);

  EXPECT_EQ(STATUS, I2cStatus::ERROR_INVALID_ARG);
}

/** @test Verify writeRead rejects zero rxLen. */
TEST(Stm32I2c, WriteReadZeroRxLen) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t tx[1] = {0x75};
  uint8_t rx[4] = {};

  const I2cStatus STATUS = i2c.writeRead(0x68, tx, 1, rx, 0);

  EXPECT_EQ(STATUS, I2cStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(Stm32I2c, InitialStatsZero) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  const auto& STATS = i2c.stats();

  EXPECT_EQ(STATS.bytesRx, 0U);
  EXPECT_EQ(STATS.bytesTx, 0U);
  EXPECT_EQ(STATS.transferCount, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify write increments stats. */
TEST(Stm32I2c, WriteIncrementsStats) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t data[5] = {};

  static_cast<void>(i2c.write(0x50, data, 5));

  EXPECT_EQ(i2c.stats().bytesTx, 5U);
  EXPECT_EQ(i2c.stats().bytesRx, 0U);
  EXPECT_EQ(i2c.stats().transferCount, 1U);
}

/** @test Verify read increments stats. */
TEST(Stm32I2c, ReadIncrementsStats) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t data[3] = {};

  static_cast<void>(i2c.read(0x50, data, 3));

  EXPECT_EQ(i2c.stats().bytesRx, 3U);
  EXPECT_EQ(i2c.stats().bytesTx, 0U);
  EXPECT_EQ(i2c.stats().transferCount, 1U);
}

/** @test Verify writeRead increments stats for both TX and RX. */
TEST(Stm32I2c, WriteReadIncrementsStats) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t tx[1] = {0x00};
  uint8_t rx[4] = {};

  static_cast<void>(i2c.writeRead(0x68, tx, 1, rx, 4));

  EXPECT_EQ(i2c.stats().bytesTx, 1U);
  EXPECT_EQ(i2c.stats().bytesRx, 4U);
  EXPECT_EQ(i2c.stats().transferCount, 1U);
}

/** @test Verify failed operations do not increment stats. */
TEST(Stm32I2c, FailedOpsNoStats) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  // Null pointer -> ERROR_INVALID_ARG, no stats increment
  static_cast<void>(i2c.write(0x50, nullptr, 4));
  static_cast<void>(i2c.read(0x50, nullptr, 4));
  static_cast<void>(i2c.writeRead(0x68, nullptr, 1, nullptr, 4));

  EXPECT_EQ(i2c.stats().bytesRx, 0U);
  EXPECT_EQ(i2c.stats().bytesTx, 0U);
  EXPECT_EQ(i2c.stats().transferCount, 0U);
}

/** @test Verify resetStats clears counters. */
TEST(Stm32I2c, ResetStats) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  uint8_t data[4] = {};
  static_cast<void>(i2c.write(0x50, data, 4));

  EXPECT_EQ(i2c.stats().bytesTx, 4U);

  i2c.resetStats();

  EXPECT_EQ(i2c.stats().bytesTx, 0U);
  EXPECT_EQ(i2c.stats().bytesRx, 0U);
  EXPECT_EQ(i2c.stats().transferCount, 0U);
}

/* ----------------------------- Stm32I2cOptions Tests ----------------------------- */

/** @test Verify Stm32I2cOptions default timeout. */
TEST(Stm32I2cOptions, DefaultValues) {
  Stm32I2cOptions opts;

  EXPECT_EQ(opts.timeoutMs, 1000U);
}

/** @test Verify Stm32I2cOptions can be aggregate-initialized. */
TEST(Stm32I2cOptions, AggregateInit) {
  Stm32I2cOptions opts = {500};

  EXPECT_EQ(opts.timeoutMs, 500U);
}

/** @test Verify init accepts Stm32I2cOptions in mock mode. */
TEST(Stm32I2c, InitWithOptions) {
  Stm32I2c i2c;
  I2cConfig config;
  config.speed = I2cSpeed::FAST_PLUS;
  Stm32I2cOptions opts = {2000};

  const I2cStatus STATUS = i2c.init(config, opts);

  EXPECT_EQ(STATUS, I2cStatus::OK);
  EXPECT_TRUE(i2c.isInitialized());
}

/* ----------------------------- isBusy Tests ----------------------------- */

/** @test Verify isBusy returns false when not initialized. */
TEST(Stm32I2c, BusyNotInit) {
  Stm32I2c i2c;
  EXPECT_FALSE(i2c.isBusy());
}

/** @test Verify isBusy returns false in mock mode (no real peripheral). */
TEST(Stm32I2c, BusyAfterInit) {
  Stm32I2c i2c;
  I2cConfig config;
  static_cast<void>(i2c.init(config));

  EXPECT_FALSE(i2c.isBusy());
}
