/**
 * @file UartConfig_uTest.cpp
 * @brief Unit tests for UART configuration types and toString functions.
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartConfig.hpp"

#include <gtest/gtest.h>
#include <type_traits>

using apex::protocols::serial::uart::BaudRate;
using apex::protocols::serial::uart::DataBits;
using apex::protocols::serial::uart::FlowControl;
using apex::protocols::serial::uart::Parity;
using apex::protocols::serial::uart::StopBits;
using apex::protocols::serial::uart::toString;
using apex::protocols::serial::uart::UartConfig;

/* ----------------------------- Enum Tests ----------------------------- */

/** @test BaudRate enum values match their numeric values. */
TEST(UartConfigTest, BaudRateValues) {
  EXPECT_EQ(static_cast<std::uint32_t>(BaudRate::B_9600), 9600u);
  EXPECT_EQ(static_cast<std::uint32_t>(BaudRate::B_115200), 115200u);
  EXPECT_EQ(static_cast<std::uint32_t>(BaudRate::B_921600), 921600u);
  EXPECT_EQ(static_cast<std::uint32_t>(BaudRate::B_4000000), 4000000u);
}

/** @test DataBits enum values match their numeric values. */
TEST(UartConfigTest, DataBitsValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(DataBits::FIVE), 5u);
  EXPECT_EQ(static_cast<std::uint8_t>(DataBits::SIX), 6u);
  EXPECT_EQ(static_cast<std::uint8_t>(DataBits::SEVEN), 7u);
  EXPECT_EQ(static_cast<std::uint8_t>(DataBits::EIGHT), 8u);
}

/** @test StopBits enum values match their numeric values. */
TEST(UartConfigTest, StopBitsValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(StopBits::ONE), 1u);
  EXPECT_EQ(static_cast<std::uint8_t>(StopBits::TWO), 2u);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test UartConfig default construction provides 8N1 at 115200. */
TEST(UartConfigTest, DefaultConstruction) {
  UartConfig cfg{};
  EXPECT_EQ(cfg.baudRate, BaudRate::B_115200);
  EXPECT_EQ(cfg.dataBits, DataBits::EIGHT);
  EXPECT_EQ(cfg.parity, Parity::NONE);
  EXPECT_EQ(cfg.stopBits, StopBits::ONE);
  EXPECT_EQ(cfg.flowControl, FlowControl::NONE);
  EXPECT_FALSE(cfg.rs485.enabled);
  EXPECT_FALSE(cfg.lowLatency);
  EXPECT_TRUE(cfg.exclusiveAccess);
}

/** @test Rs485Config default construction provides sensible defaults. */
TEST(UartConfigTest, Rs485DefaultConstruction) {
  UartConfig::Rs485Config rs485{};
  EXPECT_FALSE(rs485.enabled);
  EXPECT_TRUE(rs485.rtsOnSend);
  EXPECT_FALSE(rs485.rtsAfterSend);
  EXPECT_EQ(rs485.delayRtsBeforeSendUs, 0u);
  EXPECT_EQ(rs485.delayRtsAfterSendUs, 0u);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test BaudRate toString mappings. */
TEST(UartConfigTest, BaudRateToString) {
  EXPECT_STREQ(toString(BaudRate::B_9600), "9600");
  EXPECT_STREQ(toString(BaudRate::B_115200), "115200");
  EXPECT_STREQ(toString(BaudRate::B_921600), "921600");
  EXPECT_STREQ(toString(BaudRate::B_4000000), "4000000");
  EXPECT_STREQ(toString(static_cast<BaudRate>(12345)), "UNKNOWN");
}

/** @test DataBits toString mappings. */
TEST(UartConfigTest, DataBitsToString) {
  EXPECT_STREQ(toString(DataBits::FIVE), "5");
  EXPECT_STREQ(toString(DataBits::SIX), "6");
  EXPECT_STREQ(toString(DataBits::SEVEN), "7");
  EXPECT_STREQ(toString(DataBits::EIGHT), "8");
  EXPECT_STREQ(toString(static_cast<DataBits>(99)), "UNKNOWN");
}

/** @test Parity toString mappings. */
TEST(UartConfigTest, ParityToString) {
  EXPECT_STREQ(toString(Parity::NONE), "NONE");
  EXPECT_STREQ(toString(Parity::ODD), "ODD");
  EXPECT_STREQ(toString(Parity::EVEN), "EVEN");
  EXPECT_STREQ(toString(static_cast<Parity>(99)), "UNKNOWN");
}

/** @test StopBits toString mappings. */
TEST(UartConfigTest, StopBitsToString) {
  EXPECT_STREQ(toString(StopBits::ONE), "1");
  EXPECT_STREQ(toString(StopBits::TWO), "2");
  EXPECT_STREQ(toString(static_cast<StopBits>(99)), "UNKNOWN");
}

/** @test FlowControl toString mappings. */
TEST(UartConfigTest, FlowControlToString) {
  EXPECT_STREQ(toString(FlowControl::NONE), "NONE");
  EXPECT_STREQ(toString(FlowControl::HARDWARE), "HARDWARE");
  EXPECT_STREQ(toString(FlowControl::SOFTWARE), "SOFTWARE");
  EXPECT_STREQ(toString(static_cast<FlowControl>(99)), "UNKNOWN");
}

/* ----------------------------- Noexcept Tests ----------------------------- */

/** @test toString functions are noexcept per header contract. */
TEST(UartConfigTest, NoexceptContract) {
  static_assert(noexcept(toString(BaudRate::B_115200)), "toString(BaudRate) should be noexcept");
  static_assert(noexcept(toString(DataBits::EIGHT)), "toString(DataBits) should be noexcept");
  static_assert(noexcept(toString(Parity::NONE)), "toString(Parity) should be noexcept");
  static_assert(noexcept(toString(StopBits::ONE)), "toString(StopBits) should be noexcept");
  static_assert(noexcept(toString(FlowControl::NONE)), "toString(FlowControl) should be noexcept");
  SUCCEED();
}
