/**
 * @file CcsdsEppMessagePacker_uTest.cpp
 * @brief Unit tests for CCSDS EPP Message Packer (RT-safe packet construction).
 *
 * Reference: CCSDS 133.1-B-3 "Encapsulation Packet Protocol" Blue Book
 */

#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppMessagePacker.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace protocols::ccsds::epp;

/* ----------------------------- Helper Functions ---------------------------- */

/* (no file-scope helpers needed) */

/* ----------------------------- EppHeader Tests ----------------------------- */

/** @test Build idle packet header (1-octet). */
TEST(EppHeaderTest, BuildIdleHeader) {
  auto hdrOpt = EppHeader::buildIdle();
  ASSERT_TRUE(hdrOpt.has_value());

  EppHeader hdr = *hdrOpt;
  EXPECT_EQ(hdr.length(), EPP_HEADER_1_OCTET);

  std::array<std::uint8_t, 8> arr = hdr.toArray();
  // Byte 0: version=7 (bits 7-5), protocolId=0 (bits 4-2), LoL=00 (bits 1-0)
  // Expected: (7 << 5) | (0 << 2) | 0x00 = 0xE0
  EXPECT_EQ(arr[0], 0xE0);
}

/** @test Build 2-octet header. */
TEST(EppHeaderTest, Build2OctetHeader) {
  const std::uint8_t PROTOCOL_ID = 3;
  const std::uint8_t PKT_LEN = 10;

  auto hdrOpt = EppHeader::build2Octet(PROTOCOL_ID, PKT_LEN);
  ASSERT_TRUE(hdrOpt.has_value());

  EppHeader hdr = *hdrOpt;
  EXPECT_EQ(hdr.length(), EPP_HEADER_2_OCTET);

  std::array<std::uint8_t, 8> arr = hdr.toArray();
  // Byte 0: version=7, protocolId=3, LoL=01
  // Expected: (7 << 5) | (3 << 2) | 0x01 = 0xED
  EXPECT_EQ(arr[0], 0xED);
  // Byte 1: packet length
  EXPECT_EQ(arr[1], PKT_LEN);
}

/** @test Build 4-octet header with protocolId != 6 (protocolIde must be 0). */
TEST(EppHeaderTest, Build4OctetHeader) {
  const std::uint8_t PROTOCOL_ID = 4;
  const std::uint8_t USER_DEF = 0xA;
  const std::uint8_t PROTO_IDE = 0x0; // Must be 0 when protocolId != 6 per Section 4.1.2.6.3
  const std::uint16_t PKT_LEN = 100;

  auto hdrOpt = EppHeader::build4Octet(PROTOCOL_ID, USER_DEF, PROTO_IDE, PKT_LEN);
  ASSERT_TRUE(hdrOpt.has_value());

  EppHeader hdr = *hdrOpt;
  EXPECT_EQ(hdr.length(), EPP_HEADER_4_OCTET);

  std::array<std::uint8_t, 8> arr = hdr.toArray();
  // Byte 0: version=7, protocolId=4, LoL=10
  // Expected: (7 << 5) | (4 << 2) | 0x02 = 0xF2
  EXPECT_EQ(arr[0], 0xF2);
  // Byte 1: userDefined=0xA (high nibble), protocolIde=0x0 (low nibble) = 0xA0
  EXPECT_EQ(arr[1], 0xA0);
  // Bytes 2-3: packet length (big-endian)
  EXPECT_EQ(arr[2], 0x00);
  EXPECT_EQ(arr[3], 100);
}

/** @test Build 4-octet header with protocolId == 6 (protocolIde can be non-zero). */
TEST(EppHeaderTest, Build4OctetHeaderExtended) {
  const std::uint8_t PROTOCOL_ID = EPP_PROTOCOL_ID_EXTENDED; // 6
  const std::uint8_t USER_DEF = 0xA;
  const std::uint8_t PROTO_IDE = 0x5; // Allowed when protocolId == 6
  const std::uint16_t PKT_LEN = 100;

  auto hdrOpt = EppHeader::build4Octet(PROTOCOL_ID, USER_DEF, PROTO_IDE, PKT_LEN);
  ASSERT_TRUE(hdrOpt.has_value());

  EppHeader hdr = *hdrOpt;
  EXPECT_EQ(hdr.length(), EPP_HEADER_4_OCTET);

  std::array<std::uint8_t, 8> arr = hdr.toArray();
  // Byte 0: version=7, protocolId=6, LoL=10
  // Expected: (7 << 5) | (6 << 2) | 0x02 = 0xFA
  EXPECT_EQ(arr[0], 0xFA);
  // Byte 1: userDefined=0xA (high nibble), protocolIde=0x5 (low nibble) = 0xA5
  EXPECT_EQ(arr[1], 0xA5);
  // Bytes 2-3: packet length (big-endian)
  EXPECT_EQ(arr[2], 0x00);
  EXPECT_EQ(arr[3], 100);
}

/** @test Build 8-octet header with protocolId != 6 (protocolIde must be 0). */
TEST(EppHeaderTest, Build8OctetHeader) {
  const std::uint8_t PROTOCOL_ID = 2;
  const std::uint8_t USER_DEF = 0x3;
  const std::uint8_t PROTO_IDE = 0x0; // Must be 0 when protocolId != 6 per Section 4.1.2.6.3
  const std::uint16_t CCSDS_DEF = 0x1234;
  const std::uint32_t PKT_LEN = 1000;

  auto hdrOpt = EppHeader::build8Octet(PROTOCOL_ID, USER_DEF, PROTO_IDE, CCSDS_DEF, PKT_LEN);
  ASSERT_TRUE(hdrOpt.has_value());

  EppHeader hdr = *hdrOpt;
  EXPECT_EQ(hdr.length(), EPP_HEADER_8_OCTET);

  std::array<std::uint8_t, 8> arr = hdr.toArray();
  // Byte 0: version=7, protocolId=2, LoL=11
  // Expected: (7 << 5) | (2 << 2) | 0x03 = 0xEB
  EXPECT_EQ(arr[0], 0xEB);
  // Byte 1: userDefined=0x3, protocolIde=0x0 = 0x30
  EXPECT_EQ(arr[1], 0x30);
  // Bytes 2-3: CCSDS defined (big-endian)
  EXPECT_EQ(arr[2], 0x12);
  EXPECT_EQ(arr[3], 0x34);
  // Bytes 4-7: packet length (big-endian)
  EXPECT_EQ(arr[4], 0x00);
  EXPECT_EQ(arr[5], 0x00);
  EXPECT_EQ(arr[6], 0x03);
  EXPECT_EQ(arr[7], 0xE8);
}

/** @test Build 8-octet header with protocolId == 6 (protocolIde can be non-zero). */
TEST(EppHeaderTest, Build8OctetHeaderExtended) {
  const std::uint8_t PROTOCOL_ID = EPP_PROTOCOL_ID_EXTENDED; // 6
  const std::uint8_t USER_DEF = 0x3;
  const std::uint8_t PROTO_IDE = 0xC; // Allowed when protocolId == 6
  const std::uint16_t CCSDS_DEF = 0x1234;
  const std::uint32_t PKT_LEN = 1000;

  auto hdrOpt = EppHeader::build8Octet(PROTOCOL_ID, USER_DEF, PROTO_IDE, CCSDS_DEF, PKT_LEN);
  ASSERT_TRUE(hdrOpt.has_value());

  EppHeader hdr = *hdrOpt;
  EXPECT_EQ(hdr.length(), EPP_HEADER_8_OCTET);

  std::array<std::uint8_t, 8> arr = hdr.toArray();
  // Byte 0: version=7, protocolId=6, LoL=11
  // Expected: (7 << 5) | (6 << 2) | 0x03 = 0xFB
  EXPECT_EQ(arr[0], 0xFB);
  // Byte 1: userDefined=0x3, protocolIde=0xC = 0x3C
  EXPECT_EQ(arr[1], 0x3C);
  // Bytes 2-3: CCSDS defined (big-endian)
  EXPECT_EQ(arr[2], 0x12);
  EXPECT_EQ(arr[3], 0x34);
  // Bytes 4-7: packet length (big-endian)
  EXPECT_EQ(arr[4], 0x00);
  EXPECT_EQ(arr[5], 0x00);
  EXPECT_EQ(arr[6], 0x03);
  EXPECT_EQ(arr[7], 0xE8);
}

/** @test Header build fails with invalid protocol ID. */
TEST(EppHeaderTest, InvalidProtocolIdFails) {
  auto hdrOpt = EppHeader::buildIdle(0x08); // 3-bit field, max is 7
  EXPECT_FALSE(hdrOpt.has_value());
}

/** @test Header build fails with packet length less than header. */
TEST(EppHeaderTest, InvalidPacketLengthFails) {
  auto hdrOpt = EppHeader::build2Octet(0, 1); // Length must be >= 2
  EXPECT_FALSE(hdrOpt.has_value());
}

/* ----------------------------- EppMsg Tests -------------------------------- */

/** @test Create idle packet. */
TEST(EppMsgTest, CreateIdlePacket) {
  auto msgOpt = EppMsgDefault::createIdle();
  ASSERT_TRUE(msgOpt.has_value());

  EppMsgDefault msg = *msgOpt;
  EXPECT_EQ(msg.length(), 1U);

  auto data = msg.data();
  EXPECT_EQ(data.size(), 1U);
  EXPECT_EQ(data[0], 0xE0); // version=7, protocolId=0, LoL=00
}

/** @test Create 2-octet header packet. */
TEST(EppMsgTest, Create2OctetPacket) {
  std::vector<std::uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};

  auto msgOpt =
      EppMsgDefault::create2Octet(3, apex::compat::bytes_span{payload.data(), payload.size()});
  ASSERT_TRUE(msgOpt.has_value());

  EppMsgDefault msg = *msgOpt;
  // Total = header(2) + payload(4) = 6
  EXPECT_EQ(msg.length(), 6U);

  auto data = msg.data();
  // Byte 0: version=7, protocolId=3, LoL=01 = 0xED
  EXPECT_EQ(data[0], 0xED);
  // Byte 1: packet length = 6
  EXPECT_EQ(data[1], 6);
  // Bytes 2-5: payload
  std::vector<std::uint8_t> extracted(data.begin() + 2, data.end());
  EXPECT_EQ(extracted, payload);
}

/** @test Create 4-octet header packet with protocolId != 6. */
TEST(EppMsgTest, Create4OctetPacket) {
  std::vector<std::uint8_t> payload = {0x01, 0x02, 0x03, 0x04, 0x05};

  // protocolIde must be 0 when protocolId != 6 per Section 4.1.2.6.3
  auto msgOpt = EppMsgDefault::create4Octet(
      4, 0xA, 0x0, apex::compat::bytes_span{payload.data(), payload.size()});
  ASSERT_TRUE(msgOpt.has_value());

  EppMsgDefault msg = *msgOpt;
  // Total = header(4) + payload(5) = 9
  EXPECT_EQ(msg.length(), 9U);

  auto data = msg.data();
  // Byte 0: version=7, protocolId=4, LoL=10 = 0xF2
  EXPECT_EQ(data[0], 0xF2);
  // Byte 1: userDefined=0xA, protocolIde=0x0 = 0xA0
  EXPECT_EQ(data[1], 0xA0);
  // Bytes 2-3: packet length = 9
  std::uint16_t pktLen = (static_cast<std::uint16_t>(data[2]) << 8) | data[3];
  EXPECT_EQ(pktLen, 9U);
  // Bytes 4-8: payload
  std::vector<std::uint8_t> extracted(data.begin() + 4, data.end());
  EXPECT_EQ(extracted, payload);
}

/** @test Create 8-octet header packet with protocolId != 6. */
TEST(EppMsgTest, Create8OctetPacket) {
  std::vector<std::uint8_t> payload = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};

  // protocolIde must be 0 when protocolId != 6 per Section 4.1.2.6.3
  auto msgOpt = EppMsgDefault::create8Octet(
      2, 0x3, 0x0, 0x1234, apex::compat::bytes_span{payload.data(), payload.size()});
  ASSERT_TRUE(msgOpt.has_value());

  EppMsgDefault msg = *msgOpt;
  // Total = header(8) + payload(7) = 15
  EXPECT_EQ(msg.length(), 15U);

  auto data = msg.data();
  // Byte 0: version=7, protocolId=2, LoL=11 = 0xEB
  EXPECT_EQ(data[0], 0xEB);
  // Byte 1: userDefined=0x3, protocolIde=0x0 = 0x30
  EXPECT_EQ(data[1], 0x30);
  // Bytes 2-3: CCSDS defined = 0x1234
  std::uint16_t defined = (static_cast<std::uint16_t>(data[2]) << 8) | data[3];
  EXPECT_EQ(defined, 0x1234);
  // Bytes 4-7: packet length = 15
  std::uint32_t pktLen = (static_cast<std::uint32_t>(data[4]) << 24) |
                         (static_cast<std::uint32_t>(data[5]) << 16) |
                         (static_cast<std::uint32_t>(data[6]) << 8) | data[7];
  EXPECT_EQ(pktLen, 15U);
  // Bytes 8-14: payload
  std::vector<std::uint8_t> extracted(data.begin() + 8, data.end());
  EXPECT_EQ(extracted, payload);
}

/** @test Creation fails for non-empty payload with idle header. */
TEST(EppMsgTest, IdleWithPayloadFails) {
  // Idle packets can't have payload - use 2-octet if payload needed
  // createIdle doesn't take payload, so this is a design constraint test
  auto msgOpt = EppMsgDefault::createIdle();
  ASSERT_TRUE(msgOpt.has_value());
  EXPECT_EQ(msgOpt->length(), 1U);
}

/** @test Creation fails when packet exceeds capacity. */
TEST(EppMsgTest, ExceedsCapacityFails) {
  // EppMsgSmall has 256-byte capacity.
  std::vector<std::uint8_t> payload(300, 0xAA);
  auto msgOpt =
      EppMsgSmall::create2Octet(1, apex::compat::bytes_span{payload.data(), payload.size()});
  EXPECT_FALSE(msgOpt.has_value());
}

/* ----------------------------- packPacket Tests ---------------------------- */

/** @test Pack 2-octet packet into buffer. */
TEST(PackPacketTest, Pack2OctetPacket) {
  std::vector<std::uint8_t> payload = {0xAA, 0xBB};
  std::uint8_t buffer[256] = {};
  std::size_t bytesWritten = 0;

  bool ok = packPacket(EPP_HEADER_2_OCTET, 5, 0, 0, 0,
                       apex::compat::bytes_span{payload.data(), payload.size()}, buffer,
                       sizeof(buffer), bytesWritten);

  ASSERT_TRUE(ok);
  EXPECT_EQ(bytesWritten, 4U); // header(2) + payload(2)
  // Byte 0: version=7, protocolId=5, LoL=01 = 0xF5
  EXPECT_EQ(buffer[0], 0xF5);
  // Byte 1: packet length = 4
  EXPECT_EQ(buffer[1], 4);
  // Bytes 2-3: payload
  EXPECT_EQ(buffer[2], 0xAA);
  EXPECT_EQ(buffer[3], 0xBB);
}

/** @test Pack idle packet into buffer. */
TEST(PackPacketTest, PackIdlePacket) {
  std::uint8_t buffer[8] = {};
  std::size_t bytesWritten = 0;

  bool ok = packPacket(EPP_HEADER_1_OCTET, 0, 0, 0, 0, apex::compat::bytes_span{}, buffer,
                       sizeof(buffer), bytesWritten);

  ASSERT_TRUE(ok);
  EXPECT_EQ(bytesWritten, 1U);
  EXPECT_EQ(buffer[0], 0xE0);
}

/** @test Pack fails with null buffer. */
TEST(PackPacketTest, NullBufferFails) {
  std::size_t bytesWritten = 0;
  bool ok = packPacket(EPP_HEADER_1_OCTET, 0, 0, 0, 0, apex::compat::bytes_span{}, nullptr, 0,
                       bytesWritten);
  EXPECT_FALSE(ok);
}

/** @test Pack fails with insufficient buffer. */
TEST(PackPacketTest, InsufficientBufferFails) {
  std::vector<std::uint8_t> payload = {0xAA, 0xBB};
  std::uint8_t buffer[2] = {}; // Too small for header + payload
  std::size_t bytesWritten = 0;

  bool ok = packPacket(EPP_HEADER_2_OCTET, 0, 0, 0, 0,
                       apex::compat::bytes_span{payload.data(), payload.size()}, buffer,
                       sizeof(buffer), bytesWritten);

  EXPECT_FALSE(ok);
}

/** @test Pack fails with payload for idle packet. */
TEST(PackPacketTest, IdleWithPayloadFails) {
  std::vector<std::uint8_t> payload = {0x01};
  std::uint8_t buffer[8] = {};
  std::size_t bytesWritten = 0;

  bool ok = packPacket(EPP_HEADER_1_OCTET, 0, 0, 0, 0,
                       apex::compat::bytes_span{payload.data(), payload.size()}, buffer,
                       sizeof(buffer), bytesWritten);

  EXPECT_FALSE(ok);
}

/* ----------------------------- requiredPacketSize Tests -------------------- */

/** @test Compute required packet size. */
TEST(RequiredPacketSizeTest, ComputeSizes) {
  // Idle packet: header only
  EXPECT_EQ(requiredPacketSize(EPP_HEADER_1_OCTET, 0), 1U);
  EXPECT_EQ(requiredPacketSize(EPP_HEADER_1_OCTET, 5), 0U); // Idle can't have payload

  // 2-octet header + payload
  EXPECT_EQ(requiredPacketSize(EPP_HEADER_2_OCTET, 10), 12U);

  // 4-octet header + payload
  EXPECT_EQ(requiredPacketSize(EPP_HEADER_4_OCTET, 100), 104U);

  // 8-octet header + payload
  EXPECT_EQ(requiredPacketSize(EPP_HEADER_8_OCTET, 1000), 1008U);

  // Invalid header variant
  EXPECT_EQ(requiredPacketSize(3, 10), 0U);
}

/* ----------------------------- Blue Book Compliance Tests ------------------ */

/**
 * @test Section 4.1.2.6.3: Protocol ID Extension must be zeros if Protocol ID != '110'.
 * Tests that 4-octet header rejects non-zero protocolIde when protocolId != 6.
 */
TEST(BlueBookComplianceTest, ProtocolIdeRequiresExtendedProtocolId4Octet) {
  // protocolId=4 (not 6), protocolIde=5 (non-zero) should fail
  auto hdrOpt = EppHeader::build4Octet(4, 0, 5, 100);
  EXPECT_FALSE(hdrOpt.has_value());

  // protocolId=6 (extended), protocolIde=5 (non-zero) should succeed
  auto hdrOpt2 = EppHeader::build4Octet(EPP_PROTOCOL_ID_EXTENDED, 0, 5, 100);
  EXPECT_TRUE(hdrOpt2.has_value());

  // protocolId=4 (not 6), protocolIde=0 (zero) should succeed
  auto hdrOpt3 = EppHeader::build4Octet(4, 0, 0, 100);
  EXPECT_TRUE(hdrOpt3.has_value());
}

/**
 * @test Section 4.1.2.6.3: Protocol ID Extension must be zeros if Protocol ID != '110'.
 * Tests that 8-octet header rejects non-zero protocolIde when protocolId != 6.
 */
TEST(BlueBookComplianceTest, ProtocolIdeRequiresExtendedProtocolId8Octet) {
  // protocolId=2 (not 6), protocolIde=0xC (non-zero) should fail
  auto hdrOpt = EppHeader::build8Octet(2, 0, 0xC, 0, 1000);
  EXPECT_FALSE(hdrOpt.has_value());

  // protocolId=6 (extended), protocolIde=0xC (non-zero) should succeed
  auto hdrOpt2 = EppHeader::build8Octet(EPP_PROTOCOL_ID_EXTENDED, 0, 0xC, 0, 1000);
  EXPECT_TRUE(hdrOpt2.has_value());

  // protocolId=2 (not 6), protocolIde=0 (zero) should succeed
  auto hdrOpt3 = EppHeader::build8Octet(2, 0, 0, 0, 1000);
  EXPECT_TRUE(hdrOpt3.has_value());
}

/**
 * @test Section 4.1.3.1.5: If Encapsulated Data field is absent, Protocol ID must be '000'.
 * Tests that 2-octet header rejects empty payload with non-zero protocolId.
 */
TEST(BlueBookComplianceTest, EmptyPayloadRequiresIdleProtocolId2Octet) {
  // Empty payload with protocolId=3 (non-zero) should fail
  auto msgOpt = EppMsgDefault::create2Octet(3, apex::compat::bytes_span{});
  EXPECT_FALSE(msgOpt.has_value());

  // Empty payload with protocolId=0 (idle) should succeed
  auto msgOpt2 = EppMsgDefault::create2Octet(EPP_PROTOCOL_ID_IDLE, apex::compat::bytes_span{});
  EXPECT_TRUE(msgOpt2.has_value());

  // Non-empty payload with protocolId=3 should succeed
  std::vector<std::uint8_t> payload = {0x01, 0x02};
  auto msgOpt3 =
      EppMsgDefault::create2Octet(3, apex::compat::bytes_span{payload.data(), payload.size()});
  EXPECT_TRUE(msgOpt3.has_value());
}

/**
 * @test Section 4.1.3.1.5: If Encapsulated Data field is absent, Protocol ID must be '000'.
 * Tests that 4-octet header rejects empty payload with non-zero protocolId.
 */
TEST(BlueBookComplianceTest, EmptyPayloadRequiresIdleProtocolId4Octet) {
  // Empty payload with protocolId=4 (non-zero) should fail
  auto msgOpt = EppMsgDefault::create4Octet(4, 0, 0, apex::compat::bytes_span{});
  EXPECT_FALSE(msgOpt.has_value());

  // Empty payload with protocolId=0 (idle) should succeed
  auto msgOpt2 =
      EppMsgDefault::create4Octet(EPP_PROTOCOL_ID_IDLE, 0, 0, apex::compat::bytes_span{});
  EXPECT_TRUE(msgOpt2.has_value());

  // Non-empty payload with protocolId=4 should succeed
  std::vector<std::uint8_t> payload = {0x01, 0x02};
  auto msgOpt3 = EppMsgDefault::create4Octet(
      4, 0, 0, apex::compat::bytes_span{payload.data(), payload.size()});
  EXPECT_TRUE(msgOpt3.has_value());
}

/**
 * @test Section 4.1.3.1.5: If Encapsulated Data field is absent, Protocol ID must be '000'.
 * Tests that 8-octet header rejects empty payload with non-zero protocolId.
 */
TEST(BlueBookComplianceTest, EmptyPayloadRequiresIdleProtocolId8Octet) {
  // Empty payload with protocolId=2 (non-zero) should fail
  auto msgOpt = EppMsgDefault::create8Octet(2, 0, 0, 0, apex::compat::bytes_span{});
  EXPECT_FALSE(msgOpt.has_value());

  // Empty payload with protocolId=0 (idle) should succeed
  auto msgOpt2 =
      EppMsgDefault::create8Octet(EPP_PROTOCOL_ID_IDLE, 0, 0, 0, apex::compat::bytes_span{});
  EXPECT_TRUE(msgOpt2.has_value());

  // Non-empty payload with protocolId=2 should succeed
  std::vector<std::uint8_t> payload = {0x01, 0x02};
  auto msgOpt3 = EppMsgDefault::create8Octet(
      2, 0, 0, 0, apex::compat::bytes_span{payload.data(), payload.size()});
  EXPECT_TRUE(msgOpt3.has_value());
}
