/**
 * @file Esp32UsbCdc_uTest.cpp
 * @brief Unit tests for Esp32UsbCdc implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_ESP32_MOCK,
 * which removes ESP-IDF dependencies and allows testing the
 * buffer logic and interface compliance.
 */

#define APEX_HAL_ESP32_MOCK 1

#include "src/system/core/hal/esp32/inc/Esp32UsbCdc.hpp"

#include <gtest/gtest.h>

using apex::hal::UartConfig;
using apex::hal::UartStatus;
using apex::hal::esp32::Esp32UsbCdc;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify Esp32UsbCdc can be default constructed in mock mode. */
TEST(Esp32UsbCdc, DefaultConstruction) {
  Esp32UsbCdc<256, 256> cdc;

  EXPECT_FALSE(cdc.isInitialized());
  EXPECT_EQ(cdc.available(), 0U);
  EXPECT_FALSE(cdc.txReady());
}

/** @test Verify different buffer sizes compile. */
TEST(Esp32UsbCdc, DifferentBufferSizes) {
  Esp32UsbCdc<64, 64> small;
  Esp32UsbCdc<1024, 512> large;
  Esp32UsbCdc<128, 256> asymmetric;

  EXPECT_FALSE(small.isInitialized());
  EXPECT_FALSE(large.isInitialized());
  EXPECT_FALSE(asymmetric.isInitialized());
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(Esp32UsbCdc, InitSucceeds) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;

  const UartStatus STATUS = cdc.init(config);

  EXPECT_EQ(STATUS, UartStatus::OK);
  EXPECT_TRUE(cdc.isInitialized());
}

/** @test Verify double init returns OK without reinitializing. */
TEST(Esp32UsbCdc, DoubleInitReturnsOk) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;

  EXPECT_EQ(cdc.init(config), UartStatus::OK);
  EXPECT_TRUE(cdc.isInitialized());

  // Second init should return OK (early return)
  EXPECT_EQ(cdc.init(config), UartStatus::OK);
  EXPECT_TRUE(cdc.isInitialized());
}

/** @test Verify deinit resets state. */
TEST(Esp32UsbCdc, DeinitResetsState) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  cdc.deinit();

  EXPECT_FALSE(cdc.isInitialized());
}

/** @test Verify multiple init/deinit cycles work. */
TEST(Esp32UsbCdc, MultipleInitDeinitCycles) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(cdc.init(config), UartStatus::OK);
    EXPECT_TRUE(cdc.isInitialized());
    cdc.deinit();
    EXPECT_FALSE(cdc.isInitialized());
  }
}

/* ----------------------------- Write Tests ----------------------------- */

/** @test Verify write returns 0 when not initialized. */
TEST(Esp32UsbCdc, WriteNotInitialized) {
  Esp32UsbCdc<256, 256> cdc;
  std::uint8_t data[] = {0x01, 0x02, 0x03};

  const std::size_t WRITTEN = cdc.write(data, 3);

  EXPECT_EQ(WRITTEN, 0U);
}

/** @test Verify write queues data to TX buffer. */
TEST(Esp32UsbCdc, WriteQueuesData) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  const std::size_t WRITTEN = cdc.write(data, 4);

  EXPECT_EQ(WRITTEN, 4U);
  EXPECT_EQ(cdc.stats().bytesTx, 4U);
}

/** @test Verify write stops at buffer capacity. */
TEST(Esp32UsbCdc, WriteBufferFull) {
  Esp32UsbCdc<64, 64> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[128] = {};
  const std::size_t WRITTEN = cdc.write(data, 128);

  // Should only write 63 bytes (64 - 1 for full detection)
  EXPECT_EQ(WRITTEN, 63U);
}

/** @test Verify write returns 0 for null data. */
TEST(Esp32UsbCdc, WriteNullData) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  const std::size_t WRITTEN = cdc.write(nullptr, 10);

  EXPECT_EQ(WRITTEN, 0U);
}

/** @test Verify write returns 0 for zero length. */
TEST(Esp32UsbCdc, WriteZeroLength) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[] = {0x01};
  const std::size_t WRITTEN = cdc.write(data, 0);

  EXPECT_EQ(WRITTEN, 0U);
}

/* ----------------------------- Read Tests ----------------------------- */

/** @test Verify read returns 0 when not initialized. */
TEST(Esp32UsbCdc, ReadNotInitialized) {
  Esp32UsbCdc<256, 256> cdc;
  std::uint8_t buffer[10] = {};

  const std::size_t BYTES_READ = cdc.read(buffer, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/** @test Verify read returns 0 in mock mode (no RX injection). */
TEST(Esp32UsbCdc, ReadReturnsZero) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t buffer[10] = {};
  const std::size_t BYTES_READ = cdc.read(buffer, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/** @test Verify read returns 0 for null buffer. */
TEST(Esp32UsbCdc, ReadNullBuffer) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  const std::size_t BYTES_READ = cdc.read(nullptr, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/* ----------------------------- Available Tests ----------------------------- */

/** @test Verify available returns 0 (USB CDC does not expose buffered count). */
TEST(Esp32UsbCdc, AvailableAlwaysZero) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  EXPECT_EQ(cdc.available(), 0U);
}

/* ----------------------------- TxReady/TxComplete Tests ----------------------------- */

/** @test Verify txReady returns false when not initialized. */
TEST(Esp32UsbCdc, TxReadyNotInitialized) {
  Esp32UsbCdc<256, 256> cdc;

  EXPECT_FALSE(cdc.txReady());
}

/** @test Verify txReady returns true after init. */
TEST(Esp32UsbCdc, TxReadyAfterInit) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  EXPECT_TRUE(cdc.txReady());
}

/** @test Verify txComplete returns true when TX buffer is empty. */
TEST(Esp32UsbCdc, TxCompleteEmpty) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  EXPECT_TRUE(cdc.txComplete());
}

/** @test Verify txComplete returns false when TX buffer has data. */
TEST(Esp32UsbCdc, TxCompleteWithData) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[] = {0xAA, 0xBB};
  cdc.write(data, 2);

  EXPECT_FALSE(cdc.txComplete());
}

/* ----------------------------- Convenience Methods ----------------------------- */

/** @test Verify print writes string. */
TEST(Esp32UsbCdc, PrintString) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  const std::size_t WRITTEN = cdc.print("Test");

  EXPECT_EQ(WRITTEN, 4U);
  EXPECT_EQ(cdc.stats().bytesTx, 4U);
}

/** @test Verify writeByte writes single byte. */
TEST(Esp32UsbCdc, WriteByte) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  const std::size_t WRITTEN = cdc.writeByte(0xFF);

  EXPECT_EQ(WRITTEN, 1U);
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(Esp32UsbCdc, InitialStatsZero) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  const auto& STATS = cdc.stats();

  EXPECT_EQ(STATS.bytesRx, 0U);
  EXPECT_EQ(STATS.bytesTx, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify resetStats clears counters. */
TEST(Esp32UsbCdc, ResetStats) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03};
  cdc.write(data, 3);

  EXPECT_EQ(cdc.stats().bytesTx, 3U);

  cdc.resetStats();

  EXPECT_EQ(cdc.stats().bytesTx, 0U);
}

/* ----------------------------- Flush Tests ----------------------------- */

/** @test Verify flushRx does not crash in mock mode. */
TEST(Esp32UsbCdc, FlushRxNoCrash) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  cdc.flushRx();
  EXPECT_EQ(cdc.available(), 0U);
}

/** @test Verify flushTx does not crash in mock mode. */
TEST(Esp32UsbCdc, FlushTxNoCrash) {
  Esp32UsbCdc<256, 256> cdc;
  UartConfig config;
  static_cast<void>(cdc.init(config));

  std::uint8_t data[] = {0x01};
  cdc.write(data, 1);

  cdc.flushTx();
}
