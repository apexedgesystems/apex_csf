/**
 * @file ModbusConfig_uTest.cpp
 * @brief Unit tests for Modbus configuration structs.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusConfig.hpp"

#include <gtest/gtest.h>

namespace modbus = apex::protocols::fieldbus::modbus;

/* ----------------------------- ModbusRtuConfig ----------------------------- */

/** @test ModbusRtuConfig default values are reasonable. */
TEST(ModbusRtuConfigTest, DefaultValues) {
  modbus::ModbusRtuConfig cfg;
  EXPECT_EQ(cfg.responseTimeoutMs, 1000);
  EXPECT_EQ(cfg.interFrameDelayUs, 0); // Auto-calculate
  EXPECT_EQ(cfg.maxRetries, 3);
  EXPECT_EQ(cfg.turnaroundDelayUs, 0);
}

/** @test ModbusRtuConfig can be modified. */
TEST(ModbusRtuConfigTest, Modification) {
  modbus::ModbusRtuConfig cfg;
  cfg.responseTimeoutMs = 500;
  cfg.interFrameDelayUs = 4000;
  cfg.maxRetries = 5;
  cfg.turnaroundDelayUs = 100;

  EXPECT_EQ(cfg.responseTimeoutMs, 500);
  EXPECT_EQ(cfg.interFrameDelayUs, 4000);
  EXPECT_EQ(cfg.maxRetries, 5);
  EXPECT_EQ(cfg.turnaroundDelayUs, 100);
}

/* ----------------------------- ModbusTcpConfig ----------------------------- */

/** @test ModbusTcpConfig default values are reasonable. */
TEST(ModbusTcpConfigTest, DefaultValues) {
  modbus::ModbusTcpConfig cfg;
  EXPECT_EQ(cfg.responseTimeoutMs, 500);
  EXPECT_EQ(cfg.connectTimeoutMs, 5000);
  EXPECT_EQ(cfg.maxRetries, 2);
  EXPECT_EQ(cfg.keepAliveIntervalSec, 30);
  EXPECT_EQ(cfg.initialTransactionId, 1);
}

/** @test ModbusTcpConfig can be modified. */
TEST(ModbusTcpConfigTest, Modification) {
  modbus::ModbusTcpConfig cfg;
  cfg.responseTimeoutMs = 1000;
  cfg.connectTimeoutMs = 10000;
  cfg.maxRetries = 5;
  cfg.keepAliveIntervalSec = 60;
  cfg.initialTransactionId = 100;

  EXPECT_EQ(cfg.responseTimeoutMs, 1000);
  EXPECT_EQ(cfg.connectTimeoutMs, 10000);
  EXPECT_EQ(cfg.maxRetries, 5);
  EXPECT_EQ(cfg.keepAliveIntervalSec, 60);
  EXPECT_EQ(cfg.initialTransactionId, 100);
}

/* ----------------------------- MasterConfig ----------------------------- */

/** @test MasterConfig default values are reasonable. */
TEST(MasterConfigTest, DefaultValues) {
  modbus::MasterConfig cfg;
  EXPECT_EQ(cfg.defaultUnitAddress, 1);
  EXPECT_TRUE(cfg.validateUnitAddress);
  EXPECT_TRUE(cfg.validateFunctionCode);
}

/** @test MasterConfig can be modified. */
TEST(MasterConfigTest, Modification) {
  modbus::MasterConfig cfg;
  cfg.defaultUnitAddress = 10;
  cfg.validateUnitAddress = false;
  cfg.validateFunctionCode = false;

  EXPECT_EQ(cfg.defaultUnitAddress, 10);
  EXPECT_FALSE(cfg.validateUnitAddress);
  EXPECT_FALSE(cfg.validateFunctionCode);
}

/* ----------------------------- calculateInterFrameDelay ----------------------------- */

/** @test calculateInterFrameDelay returns correct value for 9600 baud. */
TEST(ModbusConfigTest, InterFrameDelay9600) {
  // 35 bits * 1,000,000 / 9600 = 3645.83... us
  const std::uint32_t DELAY = modbus::calculateInterFrameDelay(9600);
  EXPECT_EQ(DELAY, 3645); // Truncated
}

/** @test calculateInterFrameDelay returns correct value for 115200 baud. */
TEST(ModbusConfigTest, InterFrameDelay115200) {
  // 35 bits * 1,000,000 / 115200 = 303.81... us
  const std::uint32_t DELAY = modbus::calculateInterFrameDelay(115200);
  EXPECT_EQ(DELAY, 303); // Truncated
}

/** @test calculateInterFrameDelay returns 0 for 0 baud (avoid division by zero). */
TEST(ModbusConfigTest, InterFrameDelayZeroBaud) {
  EXPECT_EQ(modbus::calculateInterFrameDelay(0), 0);
}

/** @test calculateInterFrameDelay returns correct value for high baud rates. */
TEST(ModbusConfigTest, InterFrameDelayHighBaud) {
  // 35 bits * 1,000,000 / 921600 = 37.97... us
  const std::uint32_t DELAY = modbus::calculateInterFrameDelay(921600);
  EXPECT_EQ(DELAY, 37); // Truncated
}
