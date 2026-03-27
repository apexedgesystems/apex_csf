/**
 * @file Esp32Uart_uTest.cpp
 * @brief Unit tests for Esp32Uart implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_ESP32_MOCK,
 * which removes ESP-IDF dependencies and allows testing the
 * buffer logic and interface compliance.
 */

#define APEX_HAL_ESP32_MOCK 1

#include "src/system/core/hal/esp32/inc/Esp32Uart.hpp"

#include <gtest/gtest.h>

using apex::hal::UartConfig;
using apex::hal::UartStatus;
using apex::hal::esp32::Esp32Uart;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify Esp32Uart can be default constructed in mock mode. */
TEST(Esp32Uart, DefaultConstruction) {
  Esp32Uart<256, 256> uart;

  EXPECT_FALSE(uart.isInitialized());
  EXPECT_EQ(uart.available(), 0U);
  EXPECT_FALSE(uart.txReady());
}

/** @test Verify different buffer sizes compile. */
TEST(Esp32Uart, DifferentBufferSizes) {
  Esp32Uart<128, 128> small;
  Esp32Uart<1024, 512> large;
  Esp32Uart<256, 512> asymmetric;

  EXPECT_FALSE(small.isInitialized());
  EXPECT_FALSE(large.isInitialized());
  EXPECT_FALSE(asymmetric.isInitialized());
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(Esp32Uart, InitSucceeds) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  config.baudRate = 115200;

  const UartStatus STATUS = uart.init(config);

  EXPECT_EQ(STATUS, UartStatus::OK);
  EXPECT_TRUE(uart.isInitialized());
}

/** @test Verify double init reinitializes cleanly (deinit then init). */
TEST(Esp32Uart, DoubleInitReinitializes) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  config.baudRate = 115200;

  EXPECT_EQ(uart.init(config), UartStatus::OK);
  EXPECT_TRUE(uart.isInitialized());

  config.baudRate = 9600;
  EXPECT_EQ(uart.init(config), UartStatus::OK);
  EXPECT_TRUE(uart.isInitialized());
}

/** @test Verify deinit resets state. */
TEST(Esp32Uart, DeinitResetsState) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  uart.deinit();

  EXPECT_FALSE(uart.isInitialized());
}

/** @test Verify multiple init/deinit cycles work. */
TEST(Esp32Uart, MultipleInitDeinitCycles) {
  Esp32Uart<256, 256> uart;
  UartConfig config;

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(uart.init(config), UartStatus::OK);
    EXPECT_TRUE(uart.isInitialized());
    uart.deinit();
    EXPECT_FALSE(uart.isInitialized());
  }
}

/* ----------------------------- Write Tests ----------------------------- */

/** @test Verify write returns 0 when not initialized. */
TEST(Esp32Uart, WriteNotInitialized) {
  Esp32Uart<256, 256> uart;
  std::uint8_t data[] = {0x01, 0x02, 0x03};

  const std::size_t WRITTEN = uart.write(data, 3);

  EXPECT_EQ(WRITTEN, 0U);
}

/** @test Verify write queues data to TX buffer. */
TEST(Esp32Uart, WriteQueuesData) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  const std::size_t WRITTEN = uart.write(data, 4);

  EXPECT_EQ(WRITTEN, 4U);
  EXPECT_EQ(uart.stats().bytesTx, 4U);
}

/** @test Verify write stops at buffer capacity. */
TEST(Esp32Uart, WriteBufferFull) {
  Esp32Uart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[256] = {};
  const std::size_t WRITTEN = uart.write(data, 256);

  // Should only write 127 bytes (128 - 1 for full detection)
  EXPECT_EQ(WRITTEN, 127U);
}

/** @test Verify write returns 0 for null data. */
TEST(Esp32Uart, WriteNullData) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.write(nullptr, 10);

  EXPECT_EQ(WRITTEN, 0U);
}

/** @test Verify write returns 0 for zero length. */
TEST(Esp32Uart, WriteZeroLength) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[] = {0x01};
  const std::size_t WRITTEN = uart.write(data, 0);

  EXPECT_EQ(WRITTEN, 0U);
}

/* ----------------------------- Read Tests ----------------------------- */

/** @test Verify read returns 0 when not initialized. */
TEST(Esp32Uart, ReadNotInitialized) {
  Esp32Uart<256, 256> uart;
  std::uint8_t buffer[10] = {};

  const std::size_t BYTES_READ = uart.read(buffer, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/** @test Verify read returns 0 in mock mode (no RX injection). */
TEST(Esp32Uart, ReadReturnsZero) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t buffer[10] = {};
  const std::size_t BYTES_READ = uart.read(buffer, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/** @test Verify read returns 0 for null buffer. */
TEST(Esp32Uart, ReadNullBuffer) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t BYTES_READ = uart.read(nullptr, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/* ----------------------------- Available Tests ----------------------------- */

/** @test Verify available returns 0 when not initialized. */
TEST(Esp32Uart, AvailableNotInitialized) {
  Esp32Uart<256, 256> uart;

  EXPECT_EQ(uart.available(), 0U);
}

/** @test Verify available returns 0 in mock mode. */
TEST(Esp32Uart, AvailableReturnsZero) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  EXPECT_EQ(uart.available(), 0U);
}

/* ----------------------------- TxReady/TxComplete Tests ----------------------------- */

/** @test Verify txReady returns false when not initialized. */
TEST(Esp32Uart, TxReadyNotInitialized) {
  Esp32Uart<256, 256> uart;

  EXPECT_FALSE(uart.txReady());
}

/** @test Verify txReady returns true after init. */
TEST(Esp32Uart, TxReadyAfterInit) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  EXPECT_TRUE(uart.txReady());
}

/** @test Verify txComplete returns true when TX buffer is empty. */
TEST(Esp32Uart, TxCompleteEmpty) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  EXPECT_TRUE(uart.txComplete());
}

/** @test Verify txComplete returns false when TX buffer has data. */
TEST(Esp32Uart, TxCompleteWithData) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[] = {0xAA, 0xBB};
  uart.write(data, 2);

  EXPECT_FALSE(uart.txComplete());
}

/* ----------------------------- Convenience Methods ----------------------------- */

/** @test Verify print writes string without null terminator. */
TEST(Esp32Uart, PrintString) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.print("Hello");

  EXPECT_EQ(WRITTEN, 5U);
  EXPECT_EQ(uart.stats().bytesTx, 5U);
}

/** @test Verify println appends CRLF. */
TEST(Esp32Uart, PrintlnAppendsCrlf) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.println("Hi");

  EXPECT_EQ(WRITTEN, 4U); // "Hi" + "\r\n"
  EXPECT_EQ(uart.stats().bytesTx, 4U);
}

/** @test Verify writeByte writes single byte. */
TEST(Esp32Uart, WriteByte) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.writeByte(0xAB);

  EXPECT_EQ(WRITTEN, 1U);
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(Esp32Uart, InitialStatsZero) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const auto& STATS = uart.stats();

  EXPECT_EQ(STATS.bytesRx, 0U);
  EXPECT_EQ(STATS.bytesTx, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify resetStats clears counters. */
TEST(Esp32Uart, ResetStats) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03};
  uart.write(data, 3);

  EXPECT_EQ(uart.stats().bytesTx, 3U);

  uart.resetStats();

  EXPECT_EQ(uart.stats().bytesTx, 0U);
}

/* ----------------------------- Flush Tests ----------------------------- */

/** @test Verify flushRx does not crash in mock mode. */
TEST(Esp32Uart, FlushRxNoCrash) {
  Esp32Uart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  uart.flushRx();
  EXPECT_EQ(uart.available(), 0U);
}
