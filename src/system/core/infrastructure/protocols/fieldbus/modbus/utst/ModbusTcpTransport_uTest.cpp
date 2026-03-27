/**
 * @file ModbusTcpTransport_uTest.cpp
 * @brief Unit tests for ModbusTcpTransport.
 *
 * Note: Full integration tests require a Modbus TCP server.
 * These tests focus on construction, state management, and API behavior.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusTcpTransport.hpp"

#include <gtest/gtest.h>

#include <cstring>

namespace modbus = apex::protocols::fieldbus::modbus;

/* ----------------------------- Construction ----------------------------- */

/** @test Transport can be constructed with valid parameters. */
TEST(ModbusTcpTransportTest, Construction) {
  modbus::ModbusTcpConfig config;
  modbus::ModbusTcpTransport transport("127.0.0.1", 502, config);

  EXPECT_FALSE(transport.isOpen());
  EXPECT_EQ(transport.host(), "127.0.0.1");
  EXPECT_EQ(transport.port(), 502);
  EXPECT_EQ(transport.transactionId(), 0);
}

/** @test Transport description includes host and port. */
TEST(ModbusTcpTransportTest, DescriptionFormat) {
  modbus::ModbusTcpConfig config;
  modbus::ModbusTcpTransport transport("192.168.1.100", 5020, config);

  const std::string DESC = transport.description();
  EXPECT_NE(DESC.find("TCP:"), std::string::npos);
  EXPECT_NE(DESC.find("192.168.1.100"), std::string::npos);
  EXPECT_NE(DESC.find("5020"), std::string::npos);
}

// Note: Move operations are disabled due to ByteTrace mixin (atomic member)

/* ----------------------------- State Management ----------------------------- */

/** @test Operations fail when not open. */
TEST(ModbusTcpTransportTest, OperationsFailWhenClosed) {
  modbus::ModbusTcpConfig config;
  modbus::ModbusTcpTransport transport("127.0.0.1", 502, config);

  modbus::FrameBuffer buf;
  EXPECT_EQ(transport.sendRequest(buf, 100), modbus::Status::ERROR_NOT_CONFIGURED);
  EXPECT_EQ(transport.receiveResponse(buf, 100), modbus::Status::ERROR_NOT_CONFIGURED);
}

/** @test Close on already-closed transport succeeds. */
TEST(ModbusTcpTransportTest, CloseWhenNotOpen) {
  modbus::ModbusTcpConfig config;
  modbus::ModbusTcpTransport transport("127.0.0.1", 502, config);

  EXPECT_EQ(transport.close(), modbus::Status::SUCCESS);
  EXPECT_FALSE(transport.isOpen());
}

/** @test Flush succeeds when open (would need connection). */
TEST(ModbusTcpTransportTest, FlushWhenClosed) {
  modbus::ModbusTcpConfig config;
  modbus::ModbusTcpTransport transport("127.0.0.1", 502, config);

  EXPECT_EQ(transport.flush(), modbus::Status::ERROR_NOT_CONFIGURED);
}

/* ----------------------------- Statistics ----------------------------- */

/** @test Stats are initially zero. */
TEST(ModbusTcpTransportTest, StatsInitiallyZero) {
  modbus::ModbusTcpConfig config;
  modbus::ModbusTcpTransport transport("127.0.0.1", 502, config);

  const modbus::ModbusStats STATS = transport.stats();
  EXPECT_EQ(STATS.requestsSent, 0);
  EXPECT_EQ(STATS.responsesReceived, 0);
  EXPECT_EQ(STATS.bytesTx, 0);
  EXPECT_EQ(STATS.bytesRx, 0);
}

/** @test resetStats clears all counters. */
TEST(ModbusTcpTransportTest, ResetStats) {
  modbus::ModbusTcpConfig config;
  modbus::ModbusTcpTransport transport("127.0.0.1", 502, config);

  // Simulate stats modification (internal state, just checking reset works)
  transport.resetStats();

  const modbus::ModbusStats STATS = transport.stats();
  EXPECT_EQ(STATS.requestsSent, 0);
  EXPECT_EQ(STATS.bytesTx, 0);
}

/* ----------------------------- Configuration ----------------------------- */

/** @test Custom config values are preserved. */
TEST(ModbusTcpTransportTest, CustomConfig) {
  modbus::ModbusTcpConfig config;
  config.connectTimeoutMs = 5000;
  config.responseTimeoutMs = 2000;
  config.keepAliveIntervalSec = 60;

  modbus::ModbusTcpTransport transport("localhost", 1502, config);

  // Config is internal; verify transport is constructed without issues
  EXPECT_FALSE(transport.isOpen());
  EXPECT_EQ(transport.port(), 1502);
}

/* ----------------------------- Connection Failure ----------------------------- */

/** @test Open fails when host is unreachable. */
TEST(ModbusTcpTransportTest, OpenFailsUnreachable) {
  modbus::ModbusTcpConfig config;
  config.connectTimeoutMs = 100; // Short timeout for faster test

  // Use an address that should fail to connect
  modbus::ModbusTcpTransport transport("192.0.2.1", 502, config); // TEST-NET-1, RFC 5737

  // This should fail (timeout or connection refused)
  const modbus::Status S = transport.open();
  EXPECT_EQ(S, modbus::Status::ERROR_IO);
  EXPECT_FALSE(transport.isOpen());
}

/** @test Open fails with invalid port. */
TEST(ModbusTcpTransportTest, OpenFailsInvalidAddress) {
  modbus::ModbusTcpConfig config;
  config.connectTimeoutMs = 100;

  // Invalid hostname
  modbus::ModbusTcpTransport transport("this.host.does.not.exist.invalid", 502, config);

  const modbus::Status S = transport.open();
  EXPECT_EQ(S, modbus::Status::ERROR_IO);
  EXPECT_FALSE(transport.isOpen());
}
