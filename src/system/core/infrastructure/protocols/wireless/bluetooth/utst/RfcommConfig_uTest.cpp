/**
 * @file RfcommConfig_uTest.cpp
 * @brief Unit tests for BluetoothAddress and RfcommConfig.
 */

#include "RfcommConfig.hpp"

#include <gtest/gtest.h>

namespace bt = apex::protocols::wireless::bluetooth;

/* ----------------------------- BluetoothAddress Tests ----------------------------- */

/** @test Verify default construction creates all-zero address. */
TEST(BluetoothAddress, DefaultConstruction) {
  bt::BluetoothAddress addr;

  for (auto b : addr.bytes) {
    EXPECT_EQ(b, 0u);
  }
  EXPECT_FALSE(addr.isValid());
}

/** @test Verify fromString parses valid address. */
TEST(BluetoothAddress, FromStringValid) {
  auto addr = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");

  EXPECT_EQ(addr.bytes[0], 0xAA);
  EXPECT_EQ(addr.bytes[1], 0xBB);
  EXPECT_EQ(addr.bytes[2], 0xCC);
  EXPECT_EQ(addr.bytes[3], 0xDD);
  EXPECT_EQ(addr.bytes[4], 0xEE);
  EXPECT_EQ(addr.bytes[5], 0xFF);
  EXPECT_TRUE(addr.isValid());
}

/** @test Verify fromString handles lowercase. */
TEST(BluetoothAddress, FromStringLowercase) {
  auto addr = bt::BluetoothAddress::fromString("aa:bb:cc:dd:ee:ff");

  EXPECT_EQ(addr.bytes[0], 0xAA);
  EXPECT_EQ(addr.bytes[1], 0xBB);
  EXPECT_TRUE(addr.isValid());
}

/** @test Verify fromString handles mixed case. */
TEST(BluetoothAddress, FromStringMixedCase) {
  auto addr = bt::BluetoothAddress::fromString("aA:Bb:cC:Dd:eE:fF");

  EXPECT_EQ(addr.bytes[0], 0xAA);
  EXPECT_TRUE(addr.isValid());
}

/** @test Verify fromString rejects null pointer. */
TEST(BluetoothAddress, FromStringNull) {
  auto addr = bt::BluetoothAddress::fromString(nullptr);

  EXPECT_FALSE(addr.isValid());
}

/** @test Verify fromString rejects invalid length. */
TEST(BluetoothAddress, FromStringInvalidLength) {
  auto addr1 = bt::BluetoothAddress::fromString("AA:BB:CC");
  EXPECT_FALSE(addr1.isValid());

  auto addr2 = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF:00");
  EXPECT_FALSE(addr2.isValid());

  auto addr3 = bt::BluetoothAddress::fromString("");
  EXPECT_FALSE(addr3.isValid());
}

/** @test Verify fromString rejects invalid separators. */
TEST(BluetoothAddress, FromStringInvalidSeparator) {
  auto addr1 = bt::BluetoothAddress::fromString("AA-BB-CC-DD-EE-FF");
  EXPECT_FALSE(addr1.isValid());

  auto addr2 = bt::BluetoothAddress::fromString("AABBCCDDEEFF");
  EXPECT_FALSE(addr2.isValid());
}

/** @test Verify fromString rejects invalid hex characters. */
TEST(BluetoothAddress, FromStringInvalidHex) {
  auto addr = bt::BluetoothAddress::fromString("GG:HH:II:JJ:KK:LL");
  EXPECT_FALSE(addr.isValid());
}

/** @test Verify toString formats correctly. */
TEST(BluetoothAddress, ToString) {
  auto addr = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
  char buf[32];
  std::size_t len = addr.toString(buf, sizeof(buf));

  EXPECT_STREQ(buf, "AA:BB:CC:DD:EE:FF");
  EXPECT_EQ(len, 17u);
}

/** @test Verify toString handles small buffer. */
TEST(BluetoothAddress, ToStringSmallBuffer) {
  auto addr = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
  char buf[8];
  std::size_t len = addr.toString(buf, sizeof(buf));

  EXPECT_EQ(len, 0u);
  EXPECT_STREQ(buf, "");
}

/** @test Verify toString handles null buffer. */
TEST(BluetoothAddress, ToStringNullBuffer) {
  auto addr = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
  std::size_t len = addr.toString(nullptr, 0);

  EXPECT_EQ(len, 0u);
}

/** @test Verify isValid returns false for all-zero. */
TEST(BluetoothAddress, IsValidAllZero) {
  bt::BluetoothAddress addr{};
  EXPECT_FALSE(addr.isValid());
}

/** @test Verify isValid returns true with any non-zero byte. */
TEST(BluetoothAddress, IsValidNonZero) {
  bt::BluetoothAddress addr{};
  addr.bytes[5] = 0x01;
  EXPECT_TRUE(addr.isValid());
}

/** @test Verify isBroadcast. */
TEST(BluetoothAddress, IsBroadcast) {
  bt::BluetoothAddress broadcast{};
  for (auto& b : broadcast.bytes) {
    b = 0xFF;
  }
  EXPECT_TRUE(broadcast.isBroadcast());

  bt::BluetoothAddress notBroadcast = bt::BluetoothAddress::fromString("FF:FF:FF:FF:FF:FE");
  EXPECT_FALSE(notBroadcast.isBroadcast());
}

/** @test Verify equality operators. */
TEST(BluetoothAddress, EqualityOperators) {
  auto addr1 = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
  auto addr2 = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
  auto addr3 = bt::BluetoothAddress::fromString("11:22:33:44:55:66");

  EXPECT_TRUE(addr1 == addr2);
  EXPECT_FALSE(addr1 != addr2);
  EXPECT_FALSE(addr1 == addr3);
  EXPECT_TRUE(addr1 != addr3);
}

/* ----------------------------- RfcommConfig Tests ----------------------------- */

/** @test Verify default construction. */
TEST(RfcommConfig, DefaultConstruction) {
  bt::RfcommConfig cfg;

  EXPECT_FALSE(cfg.remoteAddress.isValid());
  EXPECT_EQ(cfg.channel, 1u);
  EXPECT_EQ(cfg.connectTimeoutMs, 5000);
  EXPECT_EQ(cfg.readTimeoutMs, 1000);
  EXPECT_EQ(cfg.writeTimeoutMs, 1000);
}

/** @test Verify isValid with valid configuration. */
TEST(RfcommConfig, IsValidWithValidConfig) {
  bt::RfcommConfig cfg;
  cfg.remoteAddress = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
  cfg.channel = 1;

  EXPECT_TRUE(cfg.isValid());
}

/** @test Verify isValid rejects invalid address. */
TEST(RfcommConfig, IsValidRejectsInvalidAddress) {
  bt::RfcommConfig cfg;
  cfg.channel = 1;

  EXPECT_FALSE(cfg.isValid());
}

/** @test Verify isValid rejects channel 0. */
TEST(RfcommConfig, IsValidRejectsChannelZero) {
  bt::RfcommConfig cfg;
  cfg.remoteAddress = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
  cfg.channel = 0;

  EXPECT_FALSE(cfg.isValid());
}

/** @test Verify isValid rejects channel > 30. */
TEST(RfcommConfig, IsValidRejectsChannelOver30) {
  bt::RfcommConfig cfg;
  cfg.remoteAddress = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
  cfg.channel = 31;

  EXPECT_FALSE(cfg.isValid());
}

/** @test Verify isValid accepts max channel. */
TEST(RfcommConfig, IsValidAcceptsMaxChannel) {
  bt::RfcommConfig cfg;
  cfg.remoteAddress = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
  cfg.channel = bt::RFCOMM_CHANNEL_MAX;

  EXPECT_TRUE(cfg.isValid());
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test Verify RFCOMM channel range constants. */
TEST(RfcommConfig, ChannelRangeConstants) {
  EXPECT_EQ(bt::RFCOMM_CHANNEL_MIN, 1u);
  EXPECT_EQ(bt::RFCOMM_CHANNEL_MAX, 30u);
}

/** @test Verify address string size constant. */
TEST(RfcommConfig, AddressStringSizeConstant) {
  EXPECT_EQ(bt::BLUETOOTH_ADDRESS_STRING_SIZE, 18u); // "XX:XX:XX:XX:XX:XX" + null
}
