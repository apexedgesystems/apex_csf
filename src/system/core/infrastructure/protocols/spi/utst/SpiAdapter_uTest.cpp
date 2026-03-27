/**
 * @file SpiAdapter_uTest.cpp
 * @brief Unit tests for SPI adapter.
 */

#include "src/system/core/infrastructure/protocols/spi/inc/SpiAdapter.hpp"

#include <gtest/gtest.h>

using apex::protocols::spi::SpiAdapter;
using apex::protocols::spi::SpiConfig;
using apex::protocols::spi::SpiMode;
using apex::protocols::spi::Status;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify adapter can be constructed with device path. */
TEST(SpiAdapter, ConstructWithPath) {
  SpiAdapter adapter("/dev/spidev0.0");

  EXPECT_STREQ(adapter.devicePath(), "/dev/spidev0.0");
  EXPECT_FALSE(adapter.isOpen());
  EXPECT_EQ(adapter.fd(), -1);
}

/** @test Verify adapter can be constructed with bus and chip-select. */
TEST(SpiAdapter, ConstructWithBusCs) {
  SpiAdapter adapter(1, 2);

  EXPECT_STREQ(adapter.devicePath(), "/dev/spidev1.2");
  EXPECT_FALSE(adapter.isOpen());
}

/** @test Verify move construction. */
TEST(SpiAdapter, MoveConstruction) {
  SpiAdapter adapter1("/dev/spidev0.0");
  SpiAdapter adapter2(std::move(adapter1));

  EXPECT_STREQ(adapter2.devicePath(), "/dev/spidev0.0");
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(SpiAdapter, InitialStatsZero) {
  SpiAdapter adapter("/dev/spidev0.0");
  const auto& stats = adapter.stats();

  EXPECT_EQ(stats.bytesRx, 0U);
  EXPECT_EQ(stats.bytesTx, 0U);
  EXPECT_EQ(stats.transfersCompleted, 0U);
  EXPECT_EQ(stats.transferWouldBlock, 0U);
  EXPECT_EQ(stats.transferErrors, 0U);
}

/** @test Verify stats reset. */
TEST(SpiAdapter, StatsReset) {
  SpiAdapter adapter("/dev/spidev0.0");

  // Stats are already zero, but verify reset works
  adapter.resetStats();
  const auto& stats = adapter.stats();

  EXPECT_EQ(stats.totalBytes(), 0U);
  EXPECT_EQ(stats.totalErrors(), 0U);
  EXPECT_EQ(stats.totalOperations(), 0U);
}

/* ----------------------------- Config Tests ----------------------------- */

/** @test Verify default config values. */
TEST(SpiAdapter, DefaultConfig) {
  SpiAdapter adapter("/dev/spidev0.0");
  const auto& config = adapter.config();

  EXPECT_EQ(config.mode, SpiMode::MODE_0);
  EXPECT_EQ(config.bitsPerWord, 8);
  EXPECT_EQ(config.maxSpeedHz, 1000000U);
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test Verify transfer fails when not configured. */
TEST(SpiAdapter, TransferWithoutConfigure) {
  SpiAdapter adapter("/dev/spidev0.0");

  std::uint8_t txData[4] = {0x01, 0x02, 0x03, 0x04};
  std::uint8_t rxData[4] = {0};

  Status status = adapter.transfer(txData, rxData, 4, 0);
  EXPECT_EQ(status, Status::ERROR_NOT_CONFIGURED);
}

/** @test Verify close on unopened device. */
TEST(SpiAdapter, CloseWithoutOpen) {
  SpiAdapter adapter("/dev/spidev0.0");

  Status status = adapter.close();
  EXPECT_EQ(status, Status::ERROR_CLOSED);
}

/** @test Verify configure with nonexistent device. */
TEST(SpiAdapter, ConfigureNonexistentDevice) {
  SpiAdapter adapter("/dev/spidev_nonexistent_device");

  SpiConfig config;
  Status status = adapter.configure(config);

  // Should fail with closed (device not found) or IO error
  EXPECT_TRUE(status == Status::ERROR_CLOSED || status == Status::ERROR_IO ||
              status == Status::ERROR_BUSY);
  EXPECT_FALSE(adapter.isOpen());
}

/* ----------------------------- Transfer Descriptor Tests ----------------------------- */

/** @test Verify TransferDesc default values. */
TEST(SpiAdapter, TransferDescDefaults) {
  SpiAdapter::TransferDesc desc;

  EXPECT_EQ(desc.txBuf, nullptr);
  EXPECT_EQ(desc.rxBuf, nullptr);
  EXPECT_EQ(desc.length, 0U);
  EXPECT_FALSE(desc.csChange);
  EXPECT_EQ(desc.delayUsecs, 0);
}

/** @test Verify batch transfer fails when not configured. */
TEST(SpiAdapter, BatchTransferWithoutConfigure) {
  SpiAdapter adapter("/dev/spidev0.0");

  std::uint8_t txData[4] = {0x01, 0x02, 0x03, 0x04};
  SpiAdapter::TransferDesc desc;
  desc.txBuf = txData;
  desc.length = 4;

  Status status = adapter.transferBatch(&desc, 1, 0);
  EXPECT_EQ(status, Status::ERROR_NOT_CONFIGURED);
}

/* ----------------------------- Zero-Length Transfer ----------------------------- */

/** @test Verify zero-length transfer succeeds when configured (would need device). */
TEST(SpiAdapter, ZeroLengthDescription) {
  // Zero-length transfers should logically succeed
  // This is a design verification test - actual behavior requires device
  SpiAdapter adapter("/dev/spidev0.0");

  // Without a real device, we verify the interface accepts zero length
  // The implementation returns SUCCESS for length=0 without device access
  // (but only if configured - which we can't do without a device)
}
