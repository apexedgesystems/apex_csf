/**
 * @file CcsdsEppMutableMessage_uTest.cpp
 * @brief Unit tests for CCSDS EPP MutableMessage (typed packet assembly facade).
 *
 * Reference: CCSDS 133.1-B-3 "Encapsulation Packet Protocol" Blue Book
 */

#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppMutableMessage.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace protocols::ccsds::epp;

/* ----------------------------- Test Payload Types -------------------------- */

/// @brief Simple 4-byte payload for testing.
struct TestPayload4 {
  std::uint32_t value;
};

/// @brief Simple 2-byte payload for testing.
struct TestPayload2 {
  std::uint16_t value;
};

/* ----------------------------- MutableEppMessageT Tests -------------------- */

/** @test Create mutable message and pack using pack(). */
TEST(MutableEppMessageTest, PackToEppMsg) {
  TestPayload4 payload{0x12345678};

  MutableEppMessageT<TestPayload4> msg;
  msg.hdr.headerVariant = EPP_HEADER_2_OCTET;
  msg.hdr.protocolId = 5;
  msg.setPayload(&payload, 1);

  auto packedOpt = msg.pack();
  ASSERT_TRUE(packedOpt.has_value());

  auto& packed = *packedOpt;
  // Total = header(2) + payload(4) = 6
  EXPECT_EQ(packed.length(), 6U);

  auto data = packed.data();
  // Byte 0: version=7, protocolId=5, LoL=01
  // (7 << 5) | (5 << 2) | 0x01 = 0xF5
  EXPECT_EQ(data[0], 0xF5);
  // Byte 1: packet length = 6
  EXPECT_EQ(data[1], 6);
  // Bytes 2-5: payload
  TestPayload4 extracted;
  std::memcpy(&extracted, data.data() + 2, sizeof(TestPayload4));
  EXPECT_EQ(extracted.value, payload.value);
}

/** @test Create mutable message and pack using packInto(). */
TEST(MutableEppMessageTest, PackIntoBuffer) {
  TestPayload4 payload{0xDEADBEEF};

  MutableEppMessageT<TestPayload4> msg;
  msg.hdr.headerVariant = EPP_HEADER_2_OCTET;
  msg.hdr.protocolId = 3;
  msg.setPayload(&payload, 1);

  std::uint8_t buffer[64] = {};
  auto bytesOpt = msg.packInto(buffer, sizeof(buffer));
  ASSERT_TRUE(bytesOpt.has_value());
  EXPECT_EQ(*bytesOpt, 6U);

  // Byte 0: version=7, protocolId=3, LoL=01 = 0xED
  EXPECT_EQ(buffer[0], 0xED);
  // Byte 1: packet length = 6
  EXPECT_EQ(buffer[1], 6);
  // Bytes 2-5: payload
  TestPayload4 extracted;
  std::memcpy(&extracted, buffer + 2, sizeof(TestPayload4));
  EXPECT_EQ(extracted.value, payload.value);
}

/** @test Pack idle packet (no payload). */
TEST(MutableEppMessageTest, PackIdlePacket) {
  MutableEppMessageT<std::uint8_t> msg;
  msg.hdr.headerVariant = EPP_HEADER_1_OCTET;
  msg.hdr.protocolId = 0;
  msg.payload = nullptr;
  msg.payloadCount = 0;

  auto packedOpt = msg.pack();
  ASSERT_TRUE(packedOpt.has_value());

  auto& packed = *packedOpt;
  EXPECT_EQ(packed.length(), 1U);

  auto data = packed.data();
  // Byte 0: version=7, protocolId=0, LoL=00 = 0xE0
  EXPECT_EQ(data[0], 0xE0);
}

/** @test Pack with 4-octet header including userDefined and protocolIde (extended protocol). */
TEST(MutableEppMessageTest, Pack4OctetHeader) {
  TestPayload2 payload{0x1234};

  MutableEppMessageT<TestPayload2> msg;
  msg.hdr.headerVariant = EPP_HEADER_4_OCTET;
  msg.hdr.protocolId = EPP_PROTOCOL_ID_EXTENDED; // Must be 6 to use non-zero protocolIde
  msg.hdr.userDefined = 0xA;
  msg.hdr.protocolIde = 0x5;
  msg.setPayload(&payload, 1);

  auto packedOpt = msg.pack();
  ASSERT_TRUE(packedOpt.has_value());

  auto& packed = *packedOpt;
  // Total = header(4) + payload(2) = 6
  EXPECT_EQ(packed.length(), 6U);

  auto data = packed.data();
  // Byte 0: version=7, protocolId=6, LoL=10 = 0xFA
  EXPECT_EQ(data[0], 0xFA);
  // Byte 1: userDefined=0xA, protocolIde=0x5 = 0xA5
  EXPECT_EQ(data[1], 0xA5);
  // Bytes 2-3: packet length = 6
  std::uint16_t pktLen = (static_cast<std::uint16_t>(data[2]) << 8) | data[3];
  EXPECT_EQ(pktLen, 6U);
}

/** @test Pack with 8-octet header including ccsdsDefined (extended protocol). */
TEST(MutableEppMessageTest, Pack8OctetHeader) {
  TestPayload4 payload{0xCAFEBABE};

  MutableEppMessageT<TestPayload4> msg;
  msg.hdr.headerVariant = EPP_HEADER_8_OCTET;
  msg.hdr.protocolId = EPP_PROTOCOL_ID_EXTENDED; // Must be 6 to use non-zero protocolIde
  msg.hdr.userDefined = 0x3;
  msg.hdr.protocolIde = 0xC;
  msg.hdr.ccsdsDefined = 0x5678;
  msg.setPayload(&payload, 1);

  auto packedOpt = msg.pack();
  ASSERT_TRUE(packedOpt.has_value());

  auto& packed = *packedOpt;
  // Total = header(8) + payload(4) = 12
  EXPECT_EQ(packed.length(), 12U);

  auto data = packed.data();
  // Byte 0: version=7, protocolId=6, LoL=11 = 0xFB
  EXPECT_EQ(data[0], 0xFB);
  // Byte 1: userDefined=0x3, protocolIde=0xC = 0x3C
  EXPECT_EQ(data[1], 0x3C);
  // Bytes 2-3: CCSDS defined = 0x5678
  std::uint16_t defined = (static_cast<std::uint16_t>(data[2]) << 8) | data[3];
  EXPECT_EQ(defined, 0x5678);
  // Bytes 4-7: packet length = 12
  std::uint32_t pktLen = (static_cast<std::uint32_t>(data[4]) << 24) |
                         (static_cast<std::uint32_t>(data[5]) << 16) |
                         (static_cast<std::uint32_t>(data[6]) << 8) | data[7];
  EXPECT_EQ(pktLen, 12U);
}

/** @test Verify requiredSize() computation. */
TEST(MutableEppMessageTest, RequiredSize) {
  TestPayload4 payload{0};

  MutableEppMessageT<TestPayload4> msg;
  msg.hdr.headerVariant = EPP_HEADER_2_OCTET;
  msg.setPayload(&payload, 1);

  EXPECT_EQ(msg.requiredSize(), 6U); // header(2) + payload(4)

  msg.hdr.headerVariant = EPP_HEADER_4_OCTET;
  EXPECT_EQ(msg.requiredSize(), 8U); // header(4) + payload(4)

  msg.hdr.headerVariant = EPP_HEADER_8_OCTET;
  EXPECT_EQ(msg.requiredSize(), 12U); // header(8) + payload(4)

  msg.hdr.headerVariant = EPP_HEADER_1_OCTET;
  msg.payloadCount = 0;
  EXPECT_EQ(msg.requiredSize(), 1U); // idle packet
}

/** @test Pack fails with non-empty payload for idle header. */
TEST(MutableEppMessageTest, IdleWithPayloadFails) {
  TestPayload4 payload{0x12345678};

  MutableEppMessageT<TestPayload4> msg;
  msg.hdr.headerVariant = EPP_HEADER_1_OCTET;
  msg.setPayload(&payload, 1);

  auto packedOpt = msg.pack();
  EXPECT_FALSE(packedOpt.has_value());
}

/** @test Pack fails with null output buffer. */
TEST(MutableEppMessageTest, PackIntoNullFails) {
  MutableEppMessageT<std::uint8_t> msg;
  msg.hdr.headerVariant = EPP_HEADER_1_OCTET;
  msg.payloadCount = 0;

  auto bytesOpt = msg.packInto(nullptr, 0);
  EXPECT_FALSE(bytesOpt.has_value());
}

/** @test Pack fails with insufficient buffer. */
TEST(MutableEppMessageTest, PackIntoInsufficientBufferFails) {
  TestPayload4 payload{0x12345678};

  MutableEppMessageT<TestPayload4> msg;
  msg.hdr.headerVariant = EPP_HEADER_2_OCTET;
  msg.setPayload(&payload, 1);

  std::uint8_t buffer[4] = {}; // Too small for 6-byte packet
  auto bytesOpt = msg.packInto(buffer, sizeof(buffer));
  EXPECT_FALSE(bytesOpt.has_value());
}

/* ----------------------------- MutableEppMessageFactory Tests -------------- */

/** @test Factory build with single payload instance. */
TEST(MutableEppFactoryTest, BuildSinglePayload) {
  TestPayload4 payload{0xABCDEF01};

  auto msgOpt =
      MutableEppMessageFactory::build<TestPayload4>(EPP_HEADER_2_OCTET, 5, 0, 0, 0, payload);
  ASSERT_TRUE(msgOpt.has_value());

  auto& msg = *msgOpt;
  auto packedOpt = msg.pack();
  ASSERT_TRUE(packedOpt.has_value());
  EXPECT_EQ(packedOpt->length(), 6U);
}

/** @test Factory build with payload span. */
TEST(MutableEppFactoryTest, BuildPayloadSpan) {
  std::vector<TestPayload2> payloads = {{0x1111}, {0x2222}};
  apex::compat::rospan<TestPayload2> span{payloads.data(), payloads.size()};

  auto msgOpt = MutableEppMessageFactory::build<TestPayload2>(EPP_HEADER_2_OCTET, 3, 0, 0, 0, span);
  ASSERT_TRUE(msgOpt.has_value());

  auto& msg = *msgOpt;
  // header(2) + 2*payload(2) = 6
  EXPECT_EQ(msg.requiredSize(), 6U);

  auto packedOpt = msg.pack();
  ASSERT_TRUE(packedOpt.has_value());
  EXPECT_EQ(packedOpt->length(), 6U);
}

/** @test Factory build with pointer and count. */
TEST(MutableEppFactoryTest, BuildPointerCount) {
  TestPayload4 payloads[] = {{0x11111111}, {0x22222222}};

  // protocolIde=0 since protocolId != 6 per Section 4.1.2.6.3
  auto msgOpt = MutableEppMessageFactory::build<TestPayload4>(EPP_HEADER_4_OCTET, 4, 0xA, 0x0, 0,
                                                              payloads, 2);
  ASSERT_TRUE(msgOpt.has_value());

  auto& msg = *msgOpt;
  // header(4) + 2*payload(4) = 12
  EXPECT_EQ(msg.requiredSize(), 12U);
}

/** @test Factory buildIdle creates idle packet. */
TEST(MutableEppFactoryTest, BuildIdle) {
  auto msgOpt = MutableEppMessageFactory::buildIdle<>();
  ASSERT_TRUE(msgOpt.has_value());

  auto& msg = *msgOpt;
  auto packedOpt = msg.pack();
  ASSERT_TRUE(packedOpt.has_value());
  EXPECT_EQ(packedOpt->length(), 1U);

  auto data = packedOpt->data();
  EXPECT_EQ(data[0], 0xE0);
}

/* ----------------------------- setPayload Tests ---------------------------- */

/** @test setPayload with rospan. */
TEST(MutableEppMessageTest, SetPayloadSpan) {
  std::vector<TestPayload2> payloads = {{0xAAAA}, {0xBBBB}, {0xCCCC}};

  MutableEppMessageT<TestPayload2> msg;
  msg.hdr.headerVariant = EPP_HEADER_2_OCTET;
  msg.hdr.protocolId = 1;
  msg.setPayload(apex::compat::rospan<TestPayload2>{payloads.data(), payloads.size()});

  EXPECT_EQ(msg.payload, payloads.data());
  EXPECT_EQ(msg.payloadCount, 3U);
  EXPECT_EQ(msg.requiredSize(), 8U); // header(2) + 3*payload(2) = 8
}

/** @test setPayload with pointer and count. */
TEST(MutableEppMessageTest, SetPayloadPointerCount) {
  TestPayload4 payloads[] = {{0x11111111}};

  MutableEppMessageT<TestPayload4> msg;
  msg.hdr.headerVariant = EPP_HEADER_2_OCTET;
  msg.setPayload(payloads, 1);

  EXPECT_EQ(msg.payload, payloads);
  EXPECT_EQ(msg.payloadCount, 1U);
}
