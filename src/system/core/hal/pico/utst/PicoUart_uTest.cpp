/**
 * @file PicoUart_uTest.cpp
 * @brief Unit tests for PicoUart implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_PICO_MOCK,
 * which removes Pico SDK dependencies and allows testing the
 * buffer logic and interface compliance.
 */

#define APEX_HAL_PICO_MOCK 1

#include "src/system/core/hal/pico/inc/PicoUart.hpp"

#include <gtest/gtest.h>

using apex::hal::UartConfig;
using apex::hal::UartStatus;
using apex::hal::pico::PicoUart;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify PicoUart can be default constructed in mock mode. */
TEST(PicoUart, DefaultConstruction) {
  PicoUart<256, 256> uart;

  EXPECT_FALSE(uart.isInitialized());
  EXPECT_EQ(uart.available(), 0U);
  EXPECT_TRUE(uart.txReady());
}

/** @test Verify different buffer sizes compile. */
TEST(PicoUart, DifferentBufferSizes) {
  PicoUart<64, 64> small;
  PicoUart<1024, 512> large;
  PicoUart<128, 256> asymmetric;

  EXPECT_EQ(small.rxCapacity(), 63U);
  EXPECT_EQ(small.txCapacity(), 63U);
  EXPECT_EQ(large.rxCapacity(), 1023U);
  EXPECT_EQ(large.txCapacity(), 511U);
  EXPECT_EQ(asymmetric.rxCapacity(), 127U);
  EXPECT_EQ(asymmetric.txCapacity(), 255U);
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(PicoUart, InitSucceeds) {
  PicoUart<256, 256> uart;
  UartConfig config;
  config.baudRate = 115200;

  const UartStatus STATUS = uart.init(config);

  EXPECT_EQ(STATUS, UartStatus::OK);
  EXPECT_TRUE(uart.isInitialized());
}

/** @test Verify double init reinitializes cleanly (double-init guard). */
TEST(PicoUart, DoubleInitReinitializes) {
  PicoUart<256, 256> uart;
  UartConfig config;
  config.baudRate = 115200;

  EXPECT_EQ(uart.init(config), UartStatus::OK);
  EXPECT_TRUE(uart.isInitialized());

  // Init again without explicit deinit -- should succeed
  config.baudRate = 9600;
  EXPECT_EQ(uart.init(config), UartStatus::OK);
  EXPECT_TRUE(uart.isInitialized());
}

/** @test Verify deinit resets state. */
TEST(PicoUart, DeinitResetsState) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  uart.deinit();

  EXPECT_FALSE(uart.isInitialized());
}

/* ----------------------------- Write Tests ----------------------------- */

/** @test Verify write returns 0 when not initialized. */
TEST(PicoUart, WriteNotInitialized) {
  PicoUart<256, 256> uart;
  std::uint8_t data[] = {0x01, 0x02, 0x03};

  const std::size_t WRITTEN = uart.write(data, 3);

  EXPECT_EQ(WRITTEN, 0U);
}

/** @test Verify write queues data to TX buffer. */
TEST(PicoUart, WriteQueuesData) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  const std::size_t WRITTEN = uart.write(data, 4);

  EXPECT_EQ(WRITTEN, 4U);
  EXPECT_EQ(uart.stats().bytesTx, 4U);
}

/** @test Verify write stops at buffer capacity. */
TEST(PicoUart, WriteBufferFull) {
  PicoUart<16, 16> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[32] = {};
  const std::size_t WRITTEN = uart.write(data, 32);

  // Should only write 15 bytes (16 - 1 for full detection)
  EXPECT_EQ(WRITTEN, 15U);
  EXPECT_FALSE(uart.txReady());
}

/** @test Verify write returns 0 for null data. */
TEST(PicoUart, WriteNullData) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.write(nullptr, 10);

  EXPECT_EQ(WRITTEN, 0U);
}

/** @test Verify write returns 0 for zero length. */
TEST(PicoUart, WriteZeroLength) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[] = {0x01};
  const std::size_t WRITTEN = uart.write(data, 0);

  EXPECT_EQ(WRITTEN, 0U);
}

/* ----------------------------- Read Tests ----------------------------- */

/** @test Verify read returns 0 when not initialized. */
TEST(PicoUart, ReadNotInitialized) {
  PicoUart<256, 256> uart;
  std::uint8_t buffer[10] = {};

  const std::size_t BYTES_READ = uart.read(buffer, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/** @test Verify read returns 0 when buffer empty (mock has no RX injection). */
TEST(PicoUart, ReadEmptyBuffer) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t buffer[10] = {};
  const std::size_t BYTES_READ = uart.read(buffer, 10);

  EXPECT_EQ(BYTES_READ, 0U);
  EXPECT_EQ(uart.available(), 0U);
}

/** @test Verify read returns 0 for null buffer. */
TEST(PicoUart, ReadNullBuffer) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t BYTES_READ = uart.read(nullptr, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/* ----------------------------- Available Tests ----------------------------- */

/** @test Verify available returns 0 after init (no data injected in mock). */
TEST(PicoUart, AvailableZeroAfterInit) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  EXPECT_EQ(uart.available(), 0U);
}

/* ----------------------------- TX State Tests ----------------------------- */

/** @test Verify txReady returns true when buffer has space. */
TEST(PicoUart, TxReadyWhenSpaceAvailable) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  EXPECT_TRUE(uart.txReady());
}

/** @test Verify txComplete returns true when TX buffer is empty. */
TEST(PicoUart, TxCompleteWhenEmpty) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  EXPECT_TRUE(uart.txComplete());
}

/* ----------------------------- Convenience Methods ----------------------------- */

/** @test Verify print writes string without null terminator. */
TEST(PicoUart, PrintString) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.print("Hello");

  EXPECT_EQ(WRITTEN, 5U);
  EXPECT_EQ(uart.stats().bytesTx, 5U);
}

/** @test Verify println appends CRLF. */
TEST(PicoUart, PrintlnAppendsCrlf) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.println("Hi");

  EXPECT_EQ(WRITTEN, 4U); // "Hi" + "\r\n"
  EXPECT_EQ(uart.stats().bytesTx, 4U);
}

/** @test Verify writeByte writes single byte. */
TEST(PicoUart, WriteByte) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.writeByte(0xAB);

  EXPECT_EQ(WRITTEN, 1U);
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(PicoUart, InitialStatsZero) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const auto& STATS = uart.stats();

  EXPECT_EQ(STATS.bytesRx, 0U);
  EXPECT_EQ(STATS.bytesTx, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify resetStats clears counters. */
TEST(PicoUart, ResetStats) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03};
  uart.write(data, 3);

  EXPECT_EQ(uart.stats().bytesTx, 3U);

  uart.resetStats();

  EXPECT_EQ(uart.stats().bytesTx, 0U);
}

/* ----------------------------- Flush Tests ----------------------------- */

/** @test Verify flushRx clears RX buffer. */
TEST(PicoUart, FlushRxClearsBuffer) {
  PicoUart<256, 256> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  uart.flushRx();
  EXPECT_EQ(uart.available(), 0U);
}

/* ----------------------------- Capacity Tests ----------------------------- */

/** @test Verify rxCapacity returns buffer size minus 1. */
TEST(PicoUart, RxCapacity) {
  EXPECT_EQ((PicoUart<256, 256>::rxCapacity()), 255U);
  EXPECT_EQ((PicoUart<128, 64>::rxCapacity()), 127U);
  EXPECT_EQ((PicoUart<32, 32>::rxCapacity()), 31U);
}

/** @test Verify txCapacity returns buffer size minus 1. */
TEST(PicoUart, TxCapacity) {
  EXPECT_EQ((PicoUart<256, 256>::txCapacity()), 255U);
  EXPECT_EQ((PicoUart<128, 64>::txCapacity()), 63U);
  EXPECT_EQ((PicoUart<32, 32>::txCapacity()), 31U);
}
