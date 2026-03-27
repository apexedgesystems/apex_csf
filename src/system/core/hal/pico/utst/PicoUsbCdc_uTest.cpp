/**
 * @file PicoUsbCdc_uTest.cpp
 * @brief Unit tests for PicoUsbCdc implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_PICO_MOCK,
 * which removes TinyUSB dependencies and provides a simulated
 * TX circular buffer with no RX data.
 */

#define APEX_HAL_PICO_MOCK 1

#include "src/system/core/hal/pico/inc/PicoUsbCdc.hpp"

#include <gtest/gtest.h>

using apex::hal::UartConfig;
using apex::hal::UartStatus;
using apex::hal::pico::PicoUsbCdc;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify PicoUsbCdc can be default constructed in mock mode. */
TEST(PicoUsbCdc, DefaultConstruction) {
  PicoUsbCdc<256, 256> cdc;

  EXPECT_FALSE(cdc.isInitialized());
}

/** @test Verify different buffer sizes compile. */
TEST(PicoUsbCdc, DifferentBufferSizes) {
  PicoUsbCdc<64, 64> small;
  PicoUsbCdc<1024, 512> large;
  PicoUsbCdc<128, 256> asymmetric;

  EXPECT_FALSE(small.isInitialized());
  EXPECT_FALSE(large.isInitialized());
  EXPECT_FALSE(asymmetric.isInitialized());
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(PicoUsbCdc, InitSucceeds) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;

  const UartStatus STATUS = cdc.init(config);

  EXPECT_EQ(STATUS, UartStatus::OK);
  EXPECT_TRUE(cdc.isInitialized());
}

/** @test Verify double init succeeds (USB CDC has no hardware to tear down). */
TEST(PicoUsbCdc, DoubleInitSucceeds) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;

  EXPECT_EQ(cdc.init(config), UartStatus::OK);
  EXPECT_EQ(cdc.init(config), UartStatus::OK);
  EXPECT_TRUE(cdc.isInitialized());
}

/** @test Verify deinit resets state. */
TEST(PicoUsbCdc, DeinitResetsState) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  cdc.deinit();

  EXPECT_FALSE(cdc.isInitialized());
}

/** @test Verify deinit clears TX buffer. */
TEST(PicoUsbCdc, DeinitClearsTxBuffer) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03};
  cdc.write(data, 3);

  cdc.deinit();
  static_cast<void>(cdc.init(config));

  // After reinit, txComplete should be true (buffer cleared)
  EXPECT_TRUE(cdc.txComplete());
}

/* ----------------------------- Write Tests ----------------------------- */

/** @test Verify write returns 0 when not initialized. */
TEST(PicoUsbCdc, WriteNotInitialized) {
  PicoUsbCdc<256, 256> cdc;
  std::uint8_t data[] = {0x01, 0x02, 0x03};

  const std::size_t WRITTEN = cdc.write(data, 3);

  EXPECT_EQ(WRITTEN, 0U);
}

/** @test Verify write queues data to TX buffer in mock mode. */
TEST(PicoUsbCdc, WriteQueuesData) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  const std::size_t WRITTEN = cdc.write(data, 4);

  EXPECT_EQ(WRITTEN, 4U);
  EXPECT_EQ(cdc.stats().bytesTx, 4U);
}

/** @test Verify write stops at buffer capacity. */
TEST(PicoUsbCdc, WriteBufferFull) {
  PicoUsbCdc<16, 16> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[32] = {};
  const std::size_t WRITTEN = cdc.write(data, 32);

  // Should only write 15 bytes (16 - 1 for full detection)
  EXPECT_EQ(WRITTEN, 15U);
  EXPECT_FALSE(cdc.txReady());
}

/** @test Verify write returns 0 for null data. */
TEST(PicoUsbCdc, WriteNullData) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  const std::size_t WRITTEN = cdc.write(nullptr, 10);

  EXPECT_EQ(WRITTEN, 0U);
}

/** @test Verify write returns 0 for zero length. */
TEST(PicoUsbCdc, WriteZeroLength) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[] = {0x01};
  const std::size_t WRITTEN = cdc.write(data, 0);

  EXPECT_EQ(WRITTEN, 0U);
}

/* ----------------------------- Read Tests ----------------------------- */

/** @test Verify read returns 0 when not initialized. */
TEST(PicoUsbCdc, ReadNotInitialized) {
  PicoUsbCdc<256, 256> cdc;
  std::uint8_t buffer[10] = {};

  const std::size_t BYTES_READ = cdc.read(buffer, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/** @test Verify read returns 0 in mock mode (no RX data). */
TEST(PicoUsbCdc, ReadReturnsZeroInMock) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t buffer[10] = {};
  const std::size_t BYTES_READ = cdc.read(buffer, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/** @test Verify read returns 0 for null buffer. */
TEST(PicoUsbCdc, ReadNullBuffer) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  const std::size_t BYTES_READ = cdc.read(nullptr, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/* ----------------------------- Available Tests ----------------------------- */

/** @test Verify available returns 0 in mock mode. */
TEST(PicoUsbCdc, AvailableReturnsZero) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  EXPECT_EQ(cdc.available(), 0U);
}

/** @test Verify available returns 0 when not initialized. */
TEST(PicoUsbCdc, AvailableNotInitialized) {
  PicoUsbCdc<256, 256> cdc;

  EXPECT_EQ(cdc.available(), 0U);
}

/* ----------------------------- TX State Tests ----------------------------- */

/** @test Verify txReady returns true when buffer has space. */
TEST(PicoUsbCdc, TxReadyWhenSpaceAvailable) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  EXPECT_TRUE(cdc.txReady());
}

/** @test Verify txReady returns false when not initialized. */
TEST(PicoUsbCdc, TxReadyNotInitialized) {
  PicoUsbCdc<256, 256> cdc;

  EXPECT_FALSE(cdc.txReady());
}

/** @test Verify txComplete returns true when TX buffer is empty. */
TEST(PicoUsbCdc, TxCompleteWhenEmpty) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  EXPECT_TRUE(cdc.txComplete());
}

/** @test Verify txComplete returns false after writing data. */
TEST(PicoUsbCdc, TxCompleteAfterWrite) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[] = {0x01};
  cdc.write(data, 1);

  EXPECT_FALSE(cdc.txComplete());
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(PicoUsbCdc, InitialStatsZero) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  const auto& STATS = cdc.stats();

  EXPECT_EQ(STATS.bytesRx, 0U);
  EXPECT_EQ(STATS.bytesTx, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify write increments bytesTx. */
TEST(PicoUsbCdc, WriteIncrementsStats) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03};
  cdc.write(data, 3);

  EXPECT_EQ(cdc.stats().bytesTx, 3U);
}

/** @test Verify resetStats clears counters. */
TEST(PicoUsbCdc, ResetStats) {
  PicoUsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03};
  cdc.write(data, 3);

  EXPECT_EQ(cdc.stats().bytesTx, 3U);

  cdc.resetStats();

  EXPECT_EQ(cdc.stats().bytesTx, 0U);
  EXPECT_EQ(cdc.stats().bytesRx, 0U);
}
