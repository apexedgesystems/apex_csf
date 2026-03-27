/**
 * @file LinConfig_uTest.cpp
 * @brief Unit tests for LinConfig and LinScheduleEntry.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinConfig.hpp"

#include <gtest/gtest.h>

namespace lin = apex::protocols::fieldbus::lin;

/* ----------------------------- Default Construction ----------------------------- */

/** @test LinConfig has sensible defaults. */
TEST(LinConfigTest, DefaultValues) {
  lin::LinConfig cfg;
  EXPECT_EQ(cfg.baudRate, 19200);
  EXPECT_EQ(cfg.checksumType, lin::ChecksumType::ENHANCED);
  EXPECT_EQ(cfg.breakThreshold, 11);
  EXPECT_EQ(cfg.interByteTimeoutBits, 14);
  EXPECT_EQ(cfg.responseTimeoutMs, 50);
  EXPECT_TRUE(cfg.enableCollisionDetection);
}

/* ----------------------------- UART Conversion ----------------------------- */

/** @test toUartConfig produces correct settings. */
TEST(LinConfigTest, ToUartConfig) {
  lin::LinConfig cfg;
  cfg.baudRate = 9600;

  auto uart = cfg.toUartConfig();
  EXPECT_EQ(uart.baudRate, apex::protocols::serial::uart::BaudRate::B_9600);
  EXPECT_EQ(uart.dataBits, apex::protocols::serial::uart::DataBits::EIGHT);
  EXPECT_EQ(uart.parity, apex::protocols::serial::uart::Parity::NONE);
  EXPECT_EQ(uart.stopBits, apex::protocols::serial::uart::StopBits::ONE);
  EXPECT_EQ(uart.flowControl, apex::protocols::serial::uart::FlowControl::NONE);
}

/** @test toUartConfig maps baud rates to closest standard rate. */
TEST(LinConfigTest, ToUartConfigBaudRates) {
  lin::LinConfig cfg;

  cfg.baudRate = 9600;
  EXPECT_EQ(cfg.toUartConfig().baudRate, apex::protocols::serial::uart::BaudRate::B_9600);

  // 10417 is between 9600 and 19200, maps to 19200
  cfg.baudRate = 10417;
  EXPECT_EQ(cfg.toUartConfig().baudRate, apex::protocols::serial::uart::BaudRate::B_19200);

  cfg.baudRate = 19200;
  EXPECT_EQ(cfg.toUartConfig().baudRate, apex::protocols::serial::uart::BaudRate::B_19200);

  // Low rates map to lower standard rates
  cfg.baudRate = 2400;
  EXPECT_EQ(cfg.toUartConfig().baudRate, apex::protocols::serial::uart::BaudRate::B_2400);

  cfg.baudRate = 4800;
  EXPECT_EQ(cfg.toUartConfig().baudRate, apex::protocols::serial::uart::BaudRate::B_4800);
}

/* ----------------------------- Timing Calculations ----------------------------- */

/** @test Inter-byte timeout calculation at 19200 baud. */
TEST(LinConfigTest, InterByteTimeoutUs19200) {
  lin::LinConfig cfg;
  cfg.baudRate = 19200;
  cfg.interByteTimeoutBits = 14;

  // 14 bits at 19200 baud = 14 * (1000000 / 19200) = ~729 us
  const std::uint32_t TIMEOUT = cfg.interByteTimeoutUs();
  EXPECT_GE(TIMEOUT, 700);
  EXPECT_LE(TIMEOUT, 750);
}

/** @test Inter-byte timeout calculation at 9600 baud. */
TEST(LinConfigTest, InterByteTimeoutUs9600) {
  lin::LinConfig cfg;
  cfg.baudRate = 9600;
  cfg.interByteTimeoutBits = 14;

  // 14 bits at 9600 baud = 14 * (1000000 / 9600) = ~1458 us
  const std::uint32_t TIMEOUT = cfg.interByteTimeoutUs();
  EXPECT_GE(TIMEOUT, 1400);
  EXPECT_LE(TIMEOUT, 1500);
}

/** @test Frame slot time calculation for 2-byte frame. */
TEST(LinConfigTest, FrameSlotTime2Bytes) {
  lin::LinConfig cfg;
  cfg.baudRate = 19200;

  // Header (33 bits) + Response (3 bytes * 10 bits = 30 bits) = 63 bits
  // At 19200 baud: 63 * (1000000 / 19200) = ~3281 us nominal
  // With 40% tolerance: ~4594 us
  const std::uint32_t SLOT_TIME = cfg.frameSlotTimeUs(2);
  EXPECT_GE(SLOT_TIME, 4000);
  EXPECT_LE(SLOT_TIME, 5000);
}

/** @test Frame slot time calculation for 8-byte frame. */
TEST(LinConfigTest, FrameSlotTime8Bytes) {
  lin::LinConfig cfg;
  cfg.baudRate = 19200;

  // Header (33 bits) + Response (9 bytes * 10 bits = 90 bits) = 123 bits
  // At 19200 baud: 123 * (1000000 / 19200) = ~6406 us nominal
  // With 40% tolerance: ~8968 us
  const std::uint32_t SLOT_TIME = cfg.frameSlotTimeUs(8);
  EXPECT_GE(SLOT_TIME, 8000);
  EXPECT_LE(SLOT_TIME, 10000);
}

/* ----------------------------- LinScheduleEntry ----------------------------- */

/** @test LinScheduleEntry default values. */
TEST(LinScheduleEntryTest, DefaultValues) {
  lin::LinScheduleEntry entry;
  EXPECT_EQ(entry.frameId, 0);
  EXPECT_EQ(entry.slotTimeMs, 10);
  EXPECT_EQ(entry.dataLength, 0);
}

/** @test LinScheduleEntry validity check. */
TEST(LinScheduleEntryTest, IsValid) {
  lin::LinScheduleEntry entry;

  entry.frameId = 0;
  EXPECT_TRUE(entry.isValid());

  entry.frameId = 63;
  EXPECT_TRUE(entry.isValid());

  entry.frameId = 64;
  EXPECT_FALSE(entry.isValid());
}

/** @test LinScheduleEntry effective data length from explicit setting. */
TEST(LinScheduleEntryTest, EffectiveDataLengthExplicit) {
  lin::LinScheduleEntry entry;
  entry.frameId = 0x10;
  entry.dataLength = 4;

  EXPECT_EQ(entry.effectiveDataLength(), 4);
}

/** @test LinScheduleEntry effective data length from ID (auto). */
TEST(LinScheduleEntryTest, EffectiveDataLengthFromId) {
  lin::LinScheduleEntry entry;
  entry.dataLength = 0; // Auto-detect

  entry.frameId = 0x00; // Should be 2 bytes
  EXPECT_EQ(entry.effectiveDataLength(), 2);

  entry.frameId = 0x20; // Should be 4 bytes
  EXPECT_EQ(entry.effectiveDataLength(), 4);

  entry.frameId = 0x30; // Should be 8 bytes
  EXPECT_EQ(entry.effectiveDataLength(), 8);
}

/** @test LinScheduleEntry effective data length for diagnostic frames. */
TEST(LinScheduleEntryTest, EffectiveDataLengthDiagnostic) {
  lin::LinScheduleEntry entry;
  entry.dataLength = 0; // Auto-detect

  entry.frameId = lin::Constants::MASTER_REQUEST_ID;
  EXPECT_EQ(entry.effectiveDataLength(), 8);

  entry.frameId = lin::Constants::SLAVE_RESPONSE_ID;
  EXPECT_EQ(entry.effectiveDataLength(), 8);
}

/** @test LinScheduleEntry rejects oversized explicit data length. */
TEST(LinScheduleEntryTest, EffectiveDataLengthOversized) {
  lin::LinScheduleEntry entry;
  entry.frameId = 0x10;
  entry.dataLength = 16; // Too large, should fall back to ID-based

  // Falls back to dataLengthFromId(0x10) = 2
  EXPECT_EQ(entry.effectiveDataLength(), 2);
}
