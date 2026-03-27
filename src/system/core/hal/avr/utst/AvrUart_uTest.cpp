/**
 * @file AvrUart_uTest.cpp
 * @brief Unit tests for AvrUart implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_AVR_MOCK,
 * which removes AVR hardware dependencies and allows testing the
 * buffer logic and interface compliance.
 */

#define APEX_HAL_AVR_MOCK 1

#include "src/system/core/hal/avr/inc/AvrUart.hpp"

#include <gtest/gtest.h>

using apex::hal::UartConfig;
using apex::hal::UartStatus;
using apex::hal::avr::AvrUart;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify AvrUart can be default constructed in mock mode. */
TEST(AvrUart, DefaultConstruction) {
  AvrUart<128, 128> uart;

  EXPECT_FALSE(uart.isInitialized());
  EXPECT_EQ(uart.available(), 0U);
  EXPECT_TRUE(uart.txReady());
}

/** @test Verify different buffer sizes compile and report correct capacity. */
TEST(AvrUart, DifferentBufferSizes) {
  AvrUart<64, 64> small;
  AvrUart<256, 128> large;
  AvrUart<128, 256> asymmetric;

  EXPECT_EQ(small.rxCapacity(), 63U);
  EXPECT_EQ(small.txCapacity(), 63U);
  EXPECT_EQ(large.rxCapacity(), 255U);
  EXPECT_EQ(large.txCapacity(), 127U);
  EXPECT_EQ(asymmetric.rxCapacity(), 127U);
  EXPECT_EQ(asymmetric.txCapacity(), 255U);
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(AvrUart, InitSucceeds) {
  AvrUart<128, 128> uart;
  UartConfig config;
  config.baudRate = 115200;

  const UartStatus STATUS = uart.init(config);

  EXPECT_EQ(STATUS, UartStatus::OK);
  EXPECT_TRUE(uart.isInitialized());
}

/** @test Verify double init reinitializes cleanly (double-init guard). */
TEST(AvrUart, DoubleInitReinitializes) {
  AvrUart<128, 128> uart;
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
TEST(AvrUart, DeinitResetsState) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  uart.deinit();

  EXPECT_FALSE(uart.isInitialized());
}

/** @test Verify multiple init/deinit cycles work. */
TEST(AvrUart, MultipleInitDeinitCycles) {
  AvrUart<128, 128> uart;
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
TEST(AvrUart, WriteNotInitialized) {
  AvrUart<128, 128> uart;
  std::uint8_t data[] = {0x01, 0x02, 0x03};

  const std::size_t WRITTEN = uart.write(data, 3);

  EXPECT_EQ(WRITTEN, 0U);
}

/** @test Verify write queues data to TX buffer. */
TEST(AvrUart, WriteQueuesData) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  const std::size_t WRITTEN = uart.write(data, 4);

  EXPECT_EQ(WRITTEN, 4U);
  EXPECT_EQ(uart.stats().bytesTx, 4U);
}

/** @test Verify write stops at buffer capacity. */
TEST(AvrUart, WriteBufferFull) {
  AvrUart<16, 16> uart; // Small buffer
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[32] = {};
  const std::size_t WRITTEN = uart.write(data, 32);

  // Should only write 15 bytes (16 - 1 for full detection)
  EXPECT_EQ(WRITTEN, 15U);
  EXPECT_FALSE(uart.txReady());
}

/** @test Verify write returns 0 for null data. */
TEST(AvrUart, WriteNullData) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.write(nullptr, 10);

  EXPECT_EQ(WRITTEN, 0U);
}

/** @test Verify write returns 0 for zero length. */
TEST(AvrUart, WriteZeroLength) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[] = {0x01};
  const std::size_t WRITTEN = uart.write(data, 0);

  EXPECT_EQ(WRITTEN, 0U);
}

/* ----------------------------- Read Tests ----------------------------- */

/** @test Verify read returns 0 when not initialized. */
TEST(AvrUart, ReadNotInitialized) {
  AvrUart<128, 128> uart;
  std::uint8_t buffer[10] = {};

  const std::size_t BYTES_READ = uart.read(buffer, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/** @test Verify read returns 0 when buffer empty (no ISR to inject data). */
TEST(AvrUart, ReadEmptyBuffer) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t buffer[10] = {};
  const std::size_t BYTES_READ = uart.read(buffer, 10);

  EXPECT_EQ(BYTES_READ, 0U);
  EXPECT_EQ(uart.available(), 0U);
}

/** @test Verify read returns 0 for null buffer. */
TEST(AvrUart, ReadNullBuffer) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t BYTES_READ = uart.read(nullptr, 10);

  EXPECT_EQ(BYTES_READ, 0U);
}

/** @test Verify read returns 0 for zero length. */
TEST(AvrUart, ReadZeroLength) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t buffer[10] = {};
  const std::size_t BYTES_READ = uart.read(buffer, 0);

  EXPECT_EQ(BYTES_READ, 0U);
}

/* ----------------------------- Convenience Methods ----------------------------- */

/** @test Verify print writes string without null terminator. */
TEST(AvrUart, PrintString) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.print("Hello");

  EXPECT_EQ(WRITTEN, 5U);
  EXPECT_EQ(uart.stats().bytesTx, 5U);
}

/** @test Verify println appends CRLF. */
TEST(AvrUart, PrintlnAppendsCrlf) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.println("Hi");

  EXPECT_EQ(WRITTEN, 4U); // "Hi" + "\r\n"
  EXPECT_EQ(uart.stats().bytesTx, 4U);
}

/** @test Verify print returns 0 for null string. */
TEST(AvrUart, PrintNullString) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.print(nullptr);

  EXPECT_EQ(WRITTEN, 0U);
}

/** @test Verify writeByte writes single byte. */
TEST(AvrUart, WriteByte) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const std::size_t WRITTEN = uart.writeByte(0xAB);

  EXPECT_EQ(WRITTEN, 1U);
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(AvrUart, InitialStatsZero) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  const auto& STATS = uart.stats();

  EXPECT_EQ(STATS.bytesRx, 0U);
  EXPECT_EQ(STATS.bytesTx, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify resetStats clears counters. */
TEST(AvrUart, ResetStats) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  std::uint8_t data[] = {0x01, 0x02, 0x03};
  uart.write(data, 3);

  EXPECT_EQ(uart.stats().bytesTx, 3U);

  uart.resetStats();

  EXPECT_EQ(uart.stats().bytesTx, 0U);
}

/* ----------------------------- TX State Tests ----------------------------- */

/** @test Verify txComplete returns true when TX buffer empty. */
TEST(AvrUart, TxCompleteWhenEmpty) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  EXPECT_TRUE(uart.txComplete());
}

/** @test Verify txReady returns true when TX buffer has space. */
TEST(AvrUart, TxReadyWithSpace) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  EXPECT_TRUE(uart.txReady());
}

/* ----------------------------- Flush Tests ----------------------------- */

/** @test Verify flushRx clears RX buffer. */
TEST(AvrUart, FlushRxClearsBuffer) {
  AvrUart<128, 128> uart;
  UartConfig config;
  static_cast<void>(uart.init(config));

  uart.flushRx();
  EXPECT_EQ(uart.available(), 0U);
}

/* ----------------------------- Capacity Tests ----------------------------- */

/** @test Verify rxCapacity returns buffer size minus 1. */
TEST(AvrUart, RxCapacity) {
  EXPECT_EQ((AvrUart<128, 128>::rxCapacity()), 127U);
  EXPECT_EQ((AvrUart<64, 32>::rxCapacity()), 63U);
  EXPECT_EQ((AvrUart<256, 256>::rxCapacity()), 255U);
}

/** @test Verify txCapacity returns buffer size minus 1. */
TEST(AvrUart, TxCapacity) {
  EXPECT_EQ((AvrUart<128, 128>::txCapacity()), 127U);
  EXPECT_EQ((AvrUart<64, 32>::txCapacity()), 31U);
  EXPECT_EQ((AvrUart<256, 256>::txCapacity()), 255U);
}
