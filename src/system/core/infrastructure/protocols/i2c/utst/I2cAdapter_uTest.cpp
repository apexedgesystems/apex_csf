/**
 * @file I2cAdapter_uTest.cpp
 * @brief Unit tests for I2C adapter.
 */

#include "src/system/core/infrastructure/protocols/i2c/inc/I2cAdapter.hpp"

#include <gtest/gtest.h>

using apex::protocols::i2c::AddressMode;
using apex::protocols::i2c::I2cAdapter;
using apex::protocols::i2c::I2cConfig;
using apex::protocols::i2c::Status;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify adapter can be constructed with device path. */
TEST(I2cAdapter, ConstructWithPath) {
  I2cAdapter adapter("/dev/i2c-1");

  EXPECT_STREQ(adapter.devicePath(), "/dev/i2c-1");
  EXPECT_FALSE(adapter.isOpen());
  EXPECT_EQ(adapter.fd(), -1);
}

/** @test Verify adapter can be constructed with bus number. */
TEST(I2cAdapter, ConstructWithBusNumber) {
  I2cAdapter adapter(2);

  EXPECT_STREQ(adapter.devicePath(), "/dev/i2c-2");
  EXPECT_FALSE(adapter.isOpen());
}

/** @test Verify move construction. */
TEST(I2cAdapter, MoveConstruction) {
  I2cAdapter adapter1("/dev/i2c-1");
  I2cAdapter adapter2(std::move(adapter1));

  EXPECT_STREQ(adapter2.devicePath(), "/dev/i2c-1");
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(I2cAdapter, InitialStatsZero) {
  I2cAdapter adapter("/dev/i2c-1");
  const auto& stats = adapter.stats();

  EXPECT_EQ(stats.bytesRx, 0U);
  EXPECT_EQ(stats.bytesTx, 0U);
  EXPECT_EQ(stats.readsCompleted, 0U);
  EXPECT_EQ(stats.writesCompleted, 0U);
  EXPECT_EQ(stats.readWouldBlock, 0U);
  EXPECT_EQ(stats.writeWouldBlock, 0U);
  EXPECT_EQ(stats.readErrors, 0U);
  EXPECT_EQ(stats.writeErrors, 0U);
  EXPECT_EQ(stats.nackCount, 0U);
}

/** @test Verify stats reset. */
TEST(I2cAdapter, StatsReset) {
  I2cAdapter adapter("/dev/i2c-1");

  adapter.resetStats();
  const auto& stats = adapter.stats();

  EXPECT_EQ(stats.totalBytes(), 0U);
  EXPECT_EQ(stats.totalErrors(), 0U);
  EXPECT_EQ(stats.totalOperations(), 0U);
}

/* ----------------------------- Slave Address Tests ----------------------------- */

/** @test Verify initial slave address is 0. */
TEST(I2cAdapter, InitialSlaveAddress) {
  I2cAdapter adapter("/dev/i2c-1");
  EXPECT_EQ(adapter.slaveAddress(), 0);
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test Verify read fails when not configured. */
TEST(I2cAdapter, ReadWithoutConfigure) {
  I2cAdapter adapter("/dev/i2c-1");

  std::uint8_t buffer[4] = {0};
  std::size_t bytesRead = 0;

  Status status = adapter.read(buffer, 4, bytesRead, 0);
  EXPECT_EQ(status, Status::ERROR_NOT_CONFIGURED);
}

/** @test Verify write fails when not configured. */
TEST(I2cAdapter, WriteWithoutConfigure) {
  I2cAdapter adapter("/dev/i2c-1");

  std::uint8_t data[4] = {0x01, 0x02, 0x03, 0x04};
  std::size_t bytesWritten = 0;

  Status status = adapter.write(data, 4, bytesWritten, 0);
  EXPECT_EQ(status, Status::ERROR_NOT_CONFIGURED);
}

/** @test Verify setSlaveAddress fails when not configured. */
TEST(I2cAdapter, SetSlaveAddressWithoutConfigure) {
  I2cAdapter adapter("/dev/i2c-1");

  Status status = adapter.setSlaveAddress(0x50);
  EXPECT_EQ(status, Status::ERROR_NOT_CONFIGURED);
}

/** @test Verify close on unopened device. */
TEST(I2cAdapter, CloseWithoutOpen) {
  I2cAdapter adapter("/dev/i2c-1");

  Status status = adapter.close();
  EXPECT_EQ(status, Status::ERROR_CLOSED);
}

/** @test Verify configure with nonexistent device. */
TEST(I2cAdapter, ConfigureNonexistentDevice) {
  I2cAdapter adapter("/dev/i2c_nonexistent_device");

  I2cConfig config;
  Status status = adapter.configure(config);

  // Should fail with closed (device not found) or IO error
  EXPECT_TRUE(status == Status::ERROR_CLOSED || status == Status::ERROR_IO ||
              status == Status::ERROR_BUSY);
  EXPECT_FALSE(adapter.isOpen());
}

/** @test Verify writeRead fails when not configured. */
TEST(I2cAdapter, WriteReadWithoutConfigure) {
  I2cAdapter adapter("/dev/i2c-1");

  std::uint8_t regAddr = 0x00;
  std::uint8_t buffer[4] = {0};
  std::size_t bytesRead = 0;

  Status status = adapter.writeRead(&regAddr, 1, buffer, 4, bytesRead, 0);
  EXPECT_EQ(status, Status::ERROR_NOT_CONFIGURED);
}

/* ----------------------------- Register Helper Tests ----------------------------- */

/** @test Verify readRegister fails when not configured. */
TEST(I2cAdapter, ReadRegisterWithoutConfigure) {
  I2cAdapter adapter("/dev/i2c-1");

  std::uint8_t value = 0;
  Status status = adapter.readRegister(0x00, value, 0);
  EXPECT_EQ(status, Status::ERROR_NOT_CONFIGURED);
}

/** @test Verify writeRegister fails when not configured. */
TEST(I2cAdapter, WriteRegisterWithoutConfigure) {
  I2cAdapter adapter("/dev/i2c-1");

  Status status = adapter.writeRegister(0x00, 0xFF, 0);
  EXPECT_EQ(status, Status::ERROR_NOT_CONFIGURED);
}

/* ----------------------------- Probe Tests ----------------------------- */

/** @test Verify probeDevice returns false when not configured. */
TEST(I2cAdapter, ProbeWithoutConfigure) {
  I2cAdapter adapter("/dev/i2c-1");

  EXPECT_FALSE(adapter.probeDevice());
}
