/**
 * @file Stm32Spi_uTest.cpp
 * @brief Unit tests for Stm32Spi implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_STM32_MOCK,
 * which removes STM32 HAL dependencies and allows testing the
 * interface compliance and stats tracking logic.
 */

#define APEX_HAL_STM32_MOCK 1

#include "src/system/core/hal/stm32/inc/Stm32Spi.hpp"

#include <gtest/gtest.h>

using apex::hal::SpiBitOrder;
using apex::hal::SpiConfig;
using apex::hal::SpiDataSize;
using apex::hal::SpiMode;
using apex::hal::SpiStats;
using apex::hal::SpiStatus;
using apex::hal::stm32::Stm32Spi;
using apex::hal::stm32::Stm32SpiOptions;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify Stm32Spi can be default constructed in mock mode. */
TEST(Stm32Spi, DefaultConstruction) {
  Stm32Spi spi;

  EXPECT_FALSE(spi.isInitialized());
  EXPECT_FALSE(spi.isBusy());
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(Stm32Spi, InitSucceeds) {
  Stm32Spi spi;
  SpiConfig config;
  config.maxClockHz = 1000000;

  const SpiStatus STATUS = spi.init(config);

  EXPECT_EQ(STATUS, SpiStatus::OK);
  EXPECT_TRUE(spi.isInitialized());
}

/** @test Verify init with all SPI modes succeeds. */
TEST(Stm32Spi, InitAllModes) {
  for (int m = 0; m <= 3; ++m) {
    Stm32Spi spi;
    SpiConfig config;
    config.mode = static_cast<SpiMode>(m);

    const SpiStatus STATUS = spi.init(config);

    EXPECT_EQ(STATUS, SpiStatus::OK);
    spi.deinit();
  }
}

/** @test Verify deinit resets state. */
TEST(Stm32Spi, DeinitResetsState) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  spi.deinit();

  EXPECT_FALSE(spi.isInitialized());
}

/** @test Verify multiple init/deinit cycles work. */
TEST(Stm32Spi, MultipleInitDeinitCycles) {
  Stm32Spi spi;
  SpiConfig config;

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(spi.init(config), SpiStatus::OK);
    EXPECT_TRUE(spi.isInitialized());
    spi.deinit();
    EXPECT_FALSE(spi.isInitialized());
  }
}

/** @test Verify double init reinitializes cleanly (double-init guard). */
TEST(Stm32Spi, DoubleInitReinitializes) {
  Stm32Spi spi;
  SpiConfig config;
  config.maxClockHz = 1000000;

  EXPECT_EQ(spi.init(config), SpiStatus::OK);
  EXPECT_TRUE(spi.isInitialized());

  // Init again without explicit deinit -- should succeed
  config.maxClockHz = 8000000;
  EXPECT_EQ(spi.init(config), SpiStatus::OK);
  EXPECT_TRUE(spi.isInitialized());
}

/* ----------------------------- Transfer Tests ----------------------------- */

/** @test Verify transfer returns ERROR_NOT_INIT when not initialized. */
TEST(Stm32Spi, TransferNotInitialized) {
  Stm32Spi spi;
  uint8_t tx[4] = {0x01, 0x02, 0x03, 0x04};
  uint8_t rx[4] = {};

  const SpiStatus STATUS = spi.transfer(tx, rx, 4);

  EXPECT_EQ(STATUS, SpiStatus::ERROR_NOT_INIT);
}

/** @test Verify transfer succeeds in mock mode. */
TEST(Stm32Spi, TransferSucceeds) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t tx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  uint8_t rx[4] = {};

  const SpiStatus STATUS = spi.transfer(tx, rx, 4);

  EXPECT_EQ(STATUS, SpiStatus::OK);
  // Mock echoes TX to RX (loopback behavior)
  EXPECT_EQ(rx[0], 0xDE);
  EXPECT_EQ(rx[1], 0xAD);
  EXPECT_EQ(rx[2], 0xBE);
  EXPECT_EQ(rx[3], 0xEF);
}

/** @test Verify transfer rejects null txData. */
TEST(Stm32Spi, TransferNullTx) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t rx[4] = {};

  const SpiStatus STATUS = spi.transfer(nullptr, rx, 4);

  EXPECT_EQ(STATUS, SpiStatus::ERROR_INVALID_ARG);
}

/** @test Verify transfer rejects null rxData. */
TEST(Stm32Spi, TransferNullRx) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t tx[4] = {0x01, 0x02, 0x03, 0x04};

  const SpiStatus STATUS = spi.transfer(tx, nullptr, 4);

  EXPECT_EQ(STATUS, SpiStatus::ERROR_INVALID_ARG);
}

/** @test Verify transfer rejects zero length. */
TEST(Stm32Spi, TransferZeroLength) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t tx[1] = {0x01};
  uint8_t rx[1] = {};

  const SpiStatus STATUS = spi.transfer(tx, rx, 0);

  EXPECT_EQ(STATUS, SpiStatus::ERROR_INVALID_ARG);
}

/** @test Verify single-byte transfer works. */
TEST(Stm32Spi, TransferSingleByte) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t tx = 0x9F;
  uint8_t rx = 0x00;

  const SpiStatus STATUS = spi.transfer(&tx, &rx, 1);

  EXPECT_EQ(STATUS, SpiStatus::OK);
  EXPECT_EQ(rx, 0x9F); // Mock echoes TX
}

/* ----------------------------- Write Tests ----------------------------- */

/** @test Verify write returns ERROR_NOT_INIT when not initialized. */
TEST(Stm32Spi, WriteNotInitialized) {
  Stm32Spi spi;
  uint8_t data[4] = {0x01, 0x02, 0x03, 0x04};

  const SpiStatus STATUS = spi.write(data, 4);

  EXPECT_EQ(STATUS, SpiStatus::ERROR_NOT_INIT);
}

/** @test Verify write succeeds in mock mode. */
TEST(Stm32Spi, WriteSucceeds) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t data[4] = {0xAB, 0xCD, 0xEF, 0x01};

  const SpiStatus STATUS = spi.write(data, 4);

  EXPECT_EQ(STATUS, SpiStatus::OK);
}

/** @test Verify write rejects null pointer. */
TEST(Stm32Spi, WriteNullPointer) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  const SpiStatus STATUS = spi.write(nullptr, 4);

  EXPECT_EQ(STATUS, SpiStatus::ERROR_INVALID_ARG);
}

/** @test Verify write rejects zero length. */
TEST(Stm32Spi, WriteZeroLength) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t data[1] = {0x01};

  const SpiStatus STATUS = spi.write(data, 0);

  EXPECT_EQ(STATUS, SpiStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Read Tests ----------------------------- */

/** @test Verify read returns ERROR_NOT_INIT when not initialized. */
TEST(Stm32Spi, ReadNotInitialized) {
  Stm32Spi spi;
  uint8_t data[4] = {};

  const SpiStatus STATUS = spi.read(data, 4);

  EXPECT_EQ(STATUS, SpiStatus::ERROR_NOT_INIT);
}

/** @test Verify read succeeds in mock mode. */
TEST(Stm32Spi, ReadSucceeds) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t data[4] = {};

  const SpiStatus STATUS = spi.read(data, 4);

  EXPECT_EQ(STATUS, SpiStatus::OK);
  // Mock fills with 0xFF (no slave responding)
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(data[i], 0xFF);
  }
}

/** @test Verify read rejects null pointer. */
TEST(Stm32Spi, ReadNullPointer) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  const SpiStatus STATUS = spi.read(nullptr, 4);

  EXPECT_EQ(STATUS, SpiStatus::ERROR_INVALID_ARG);
}

/** @test Verify read rejects zero length. */
TEST(Stm32Spi, ReadZeroLength) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t data[1] = {};

  const SpiStatus STATUS = spi.read(data, 0);

  EXPECT_EQ(STATUS, SpiStatus::ERROR_INVALID_ARG);
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(Stm32Spi, InitialStatsZero) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  const auto& STATS = spi.stats();

  EXPECT_EQ(STATS.bytesTransferred, 0U);
  EXPECT_EQ(STATS.transferCount, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify transfer increments stats. */
TEST(Stm32Spi, TransferIncrementsStats) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t tx[8] = {};
  uint8_t rx[8] = {};

  static_cast<void>(spi.transfer(tx, rx, 8));
  static_cast<void>(spi.transfer(tx, rx, 4));

  EXPECT_EQ(spi.stats().bytesTransferred, 12U);
  EXPECT_EQ(spi.stats().transferCount, 2U);
}

/** @test Verify write increments stats. */
TEST(Stm32Spi, WriteIncrementsStats) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t data[5] = {};

  static_cast<void>(spi.write(data, 5));

  EXPECT_EQ(spi.stats().bytesTransferred, 5U);
  EXPECT_EQ(spi.stats().transferCount, 1U);
}

/** @test Verify read increments stats. */
TEST(Stm32Spi, ReadIncrementsStats) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t data[3] = {};

  static_cast<void>(spi.read(data, 3));

  EXPECT_EQ(spi.stats().bytesTransferred, 3U);
  EXPECT_EQ(spi.stats().transferCount, 1U);
}

/** @test Verify failed operations do not increment stats. */
TEST(Stm32Spi, FailedOpsNoStats) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  // Null pointer -> ERROR_INVALID_ARG, no stats increment
  static_cast<void>(spi.transfer(nullptr, nullptr, 4));
  static_cast<void>(spi.write(nullptr, 4));
  static_cast<void>(spi.read(nullptr, 4));

  EXPECT_EQ(spi.stats().bytesTransferred, 0U);
  EXPECT_EQ(spi.stats().transferCount, 0U);
}

/** @test Verify resetStats clears counters. */
TEST(Stm32Spi, ResetStats) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  uint8_t tx[4] = {};
  uint8_t rx[4] = {};
  static_cast<void>(spi.transfer(tx, rx, 4));

  EXPECT_EQ(spi.stats().bytesTransferred, 4U);

  spi.resetStats();

  EXPECT_EQ(spi.stats().bytesTransferred, 0U);
  EXPECT_EQ(spi.stats().transferCount, 0U);
}

/* ----------------------------- Stm32SpiOptions Tests ----------------------------- */

/** @test Verify Stm32SpiOptions default timeout. */
TEST(Stm32SpiOptions, DefaultValues) {
  Stm32SpiOptions opts;

  EXPECT_EQ(opts.timeoutMs, 1000U);
}

/** @test Verify Stm32SpiOptions can be aggregate-initialized. */
TEST(Stm32SpiOptions, AggregateInit) {
  Stm32SpiOptions opts = {500};

  EXPECT_EQ(opts.timeoutMs, 500U);
}

/** @test Verify init accepts Stm32SpiOptions in mock mode. */
TEST(Stm32Spi, InitWithOptions) {
  Stm32Spi spi;
  SpiConfig config;
  config.maxClockHz = 8000000;
  Stm32SpiOptions opts = {2000};

  const SpiStatus STATUS = spi.init(config, opts);

  EXPECT_EQ(STATUS, SpiStatus::OK);
  EXPECT_TRUE(spi.isInitialized());
}

/* ----------------------------- isBusy Tests ----------------------------- */

/** @test Verify isBusy returns false when not initialized. */
TEST(Stm32Spi, BusyNotInit) {
  Stm32Spi spi;
  EXPECT_FALSE(spi.isBusy());
}

/** @test Verify isBusy returns false in mock mode (no real peripheral). */
TEST(Stm32Spi, BusyAfterInit) {
  Stm32Spi spi;
  SpiConfig config;
  static_cast<void>(spi.init(config));

  EXPECT_FALSE(spi.isBusy());
}
