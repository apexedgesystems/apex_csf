/**
 * @file CcsdsEppViewer_uTest.cpp
 * @brief Unit tests for CCSDS EPP Viewer (zero-copy packet viewing).
 *
 * Reference: CCSDS 133.1-B-3 "Encapsulation Packet Protocol" Blue Book
 */

#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppViewer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace protocols::ccsds::epp;

/* ----------------------------- Helper Functions ---------------------------- */

namespace {

/// @brief Convert span to vector for comparison.
std::vector<std::uint8_t> toVector(apex::compat::bytes_span sp) {
  return std::vector<std::uint8_t>(sp.begin(), sp.end());
}

/// @brief Create EPP packet from header bytes and payload.
std::vector<std::uint8_t> createEppPacket(const std::vector<std::uint8_t>& headerBytes,
                                          const std::vector<std::uint8_t>& payload) {
  std::vector<std::uint8_t> packet;
  packet.insert(packet.end(), headerBytes.begin(), headerBytes.end());
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

} // namespace

/* ----------------------------- Idle Packet Tests --------------------------- */

/** @test Verify viewer parses idle packet (1-octet header) correctly. */
TEST(EppViewerTest, IdlePacketTest) {
  // Construct 1-octet header: version=7, protocolId=0, LoL=00
  // Byte 0: (7 << 5) | (0 << 2) | 0x00 = 0xE0
  const std::uint8_t HEADER_BYTE = 0xE0;
  std::vector<std::uint8_t> packet = {HEADER_BYTE};

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()});
  ASSERT_TRUE(viewOpt.has_value());
  auto& view = *viewOpt;

  // Verify header fields.
  EXPECT_EQ(view.hdr.version(), 7);
  EXPECT_EQ(view.hdr.protocolId(), 0);
  EXPECT_EQ(view.hdr.lengthOfLength(), EPP_LOL_IDLE);
  EXPECT_TRUE(view.hdr.isIdle());

  // Idle packet has no payload.
  EXPECT_EQ(view.encapsulatedData().size(), 0U);

  // Total packet length is 1.
  EXPECT_EQ(view.packetLength(), 1U);
  EXPECT_EQ(view.raw.size(), 1U);
}

/* ----------------------------- 2-Octet Header Tests ------------------------ */

/** @test Verify viewer parses 2-octet header packet correctly. */
TEST(EppViewerTest, TwoOctetHeaderTest) {
  // Header: version=7, protocolId=3, LoL=01
  // Byte 0: (7 << 5) | (3 << 2) | 0x01 = 0xED
  // Byte 1: Packet Length = 6 (header + payload)
  std::vector<std::uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD};
  const std::uint8_t BYTE0 = 0xED;
  const std::uint8_t TOTAL_LEN = 6;
  std::vector<std::uint8_t> header = {BYTE0, TOTAL_LEN};
  std::vector<std::uint8_t> packet = createEppPacket(header, payload);

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()});
  ASSERT_TRUE(viewOpt.has_value());
  auto& view = *viewOpt;

  EXPECT_EQ(view.hdr.version(), 7);
  EXPECT_EQ(view.hdr.protocolId(), 3);
  EXPECT_EQ(view.hdr.lengthOfLength(), EPP_LOL_1_OCTET);
  EXPECT_FALSE(view.hdr.isIdle());

  EXPECT_EQ(view.packetLength(), 6U);
  EXPECT_EQ(toVector(view.encapsulatedData()), payload);
  EXPECT_EQ(view.raw.size(), 6U);
}

/* ----------------------------- 4-Octet Header Tests ------------------------ */

/** @test Verify viewer parses 4-octet header packet correctly. */
TEST(EppViewerTest, FourOctetHeaderTest) {
  // Header: version=7, protocolId=4, LoL=10, userDefined=0xA, protocolIde=0x5
  // Byte 0: (7 << 5) | (4 << 2) | 0x02 = 0xF2
  // Byte 1: (0xA << 4) | 0x5 = 0xA5
  // Bytes 2-3: Packet Length = 9 (big-endian)
  std::vector<std::uint8_t> payload = {0x11, 0x22, 0x33, 0x44, 0x55};
  const std::uint8_t BYTE0 = 0xF2;
  const std::uint8_t BYTE1 = 0xA5;
  const std::uint8_t BYTE2 = 0x00;
  const std::uint8_t BYTE3 = 0x09;
  std::vector<std::uint8_t> header = {BYTE0, BYTE1, BYTE2, BYTE3};
  std::vector<std::uint8_t> packet = createEppPacket(header, payload);

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()});
  ASSERT_TRUE(viewOpt.has_value());
  auto& view = *viewOpt;

  EXPECT_EQ(view.hdr.version(), 7);
  EXPECT_EQ(view.hdr.protocolId(), 4);
  EXPECT_EQ(view.hdr.lengthOfLength(), EPP_LOL_2_OCTETS);
  EXPECT_EQ(view.hdr.userDefined(), 0xA);
  EXPECT_EQ(view.hdr.protocolIdExtension(), 0x5);

  EXPECT_EQ(view.packetLength(), 9U);
  EXPECT_EQ(toVector(view.encapsulatedData()), payload);
  EXPECT_EQ(view.raw.size(), 9U);
}

/* ----------------------------- 8-Octet Header Tests ------------------------ */

/** @test Verify viewer parses 8-octet header packet correctly. */
TEST(EppViewerTest, EightOctetHeaderTest) {
  // Header: version=7, protocolId=2, LoL=11, userDefined=0x3, protocolIde=0xC,
  // ccsdsDefined=0x1234
  // Byte 0: (7 << 5) | (2 << 2) | 0x03 = 0xEB
  // Byte 1: (0x3 << 4) | 0xC = 0x3C
  // Bytes 2-3: CCSDS Defined = 0x1234
  // Bytes 4-7: Packet Length = 15 (big-endian)
  std::vector<std::uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11};
  const std::uint8_t BYTE0 = 0xEB;
  const std::uint8_t BYTE1 = 0x3C;
  std::vector<std::uint8_t> header = {BYTE0, BYTE1, 0x12, 0x34, 0x00, 0x00, 0x00, 0x0F};
  std::vector<std::uint8_t> packet = createEppPacket(header, payload);

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()});
  ASSERT_TRUE(viewOpt.has_value());
  auto& view = *viewOpt;

  EXPECT_EQ(view.hdr.version(), 7);
  EXPECT_EQ(view.hdr.protocolId(), 2);
  EXPECT_EQ(view.hdr.lengthOfLength(), EPP_LOL_4_OCTETS);
  EXPECT_EQ(view.hdr.userDefined(), 0x3);
  EXPECT_EQ(view.hdr.protocolIdExtension(), 0xC);
  EXPECT_EQ(view.hdr.ccsdsDefined(), 0x1234);

  EXPECT_EQ(view.packetLength(), 15U);
  EXPECT_EQ(toVector(view.encapsulatedData()), payload);
  EXPECT_EQ(view.raw.size(), 15U);
}

/* ----------------------------- Validation Tests ---------------------------- */

/** @test Verify validation fails for empty packet. */
TEST(EppViewerTest, EmptyPacketFails) {
  std::vector<std::uint8_t> empty;
  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{empty.data(), empty.size()});
  EXPECT_FALSE(viewOpt.has_value());

  auto err = PacketViewer::validate(apex::compat::bytes_span{empty.data(), empty.size()});
  EXPECT_EQ(err, ValidationError::PACKET_TOO_SMALL);
}

/** @test Verify validation fails for invalid version. */
TEST(EppViewerTest, InvalidVersionFails) {
  // Version = 6 (should be 7)
  const std::uint8_t INVALID_VERSION_BYTE = (6 << 5) | (0 << 2) | 0x00;
  std::vector<std::uint8_t> packet = {INVALID_VERSION_BYTE};

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()});
  EXPECT_FALSE(viewOpt.has_value());

  auto err = PacketViewer::validate(apex::compat::bytes_span{packet.data(), packet.size()});
  EXPECT_EQ(err, ValidationError::INVALID_VERSION);
}

/** @test Verify validation fails for length mismatch. */
TEST(EppViewerTest, LengthMismatchFails) {
  // 2-octet header claiming length=10, but only 6 bytes provided
  const std::uint8_t BYTE0 = 0xED; // version=7, protocolId=3, LoL=01
  const std::uint8_t TOTAL_LEN = 10;
  std::vector<std::uint8_t> packet = {BYTE0, TOTAL_LEN, 0xAA, 0xBB, 0xCC, 0xDD};

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()});
  EXPECT_FALSE(viewOpt.has_value());

  auto err = PacketViewer::validate(apex::compat::bytes_span{packet.data(), packet.size()});
  EXPECT_EQ(err, ValidationError::LENGTH_MISMATCH);
}

/** @test Verify validation fails for incomplete header. */
TEST(EppViewerTest, IncompleteHeaderFails) {
  // LoL=10 requires 4-octet header, but only 2 bytes provided
  const std::uint8_t BYTE0 = 0xF2; // version=7, protocolId=4, LoL=10
  std::vector<std::uint8_t> packet = {BYTE0, 0x00};

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()});
  EXPECT_FALSE(viewOpt.has_value());

  auto err = PacketViewer::validate(apex::compat::bytes_span{packet.data(), packet.size()});
  EXPECT_EQ(err, ValidationError::HEADER_INCOMPLETE);
}

/* ----------------------------- Fast-Path Tests ----------------------------- */

/** @test Verify peekProtocolId extracts protocol ID without full parsing. */
TEST(EppViewerTest, PeekProtocolId) {
  // version=7, protocolId=5, LoL=01
  const std::uint8_t BYTE0 = (7 << 5) | (5 << 2) | 0x01;
  std::vector<std::uint8_t> packet = {BYTE0, 0x02};

  auto pidOpt =
      PacketViewer::peekProtocolId(apex::compat::bytes_span{packet.data(), packet.size()});
  ASSERT_TRUE(pidOpt.has_value());
  EXPECT_EQ(*pidOpt, 5);
}

/** @test Verify peekLoL extracts Length of Length without full parsing. */
TEST(EppViewerTest, PeekLoL) {
  // version=7, protocolId=0, LoL=11
  const std::uint8_t BYTE0 = (7 << 5) | (0 << 2) | 0x03;
  std::vector<std::uint8_t> packet = {BYTE0};

  auto lolOpt = PacketViewer::peekLoL(apex::compat::bytes_span{packet.data(), packet.size()});
  ASSERT_TRUE(lolOpt.has_value());
  EXPECT_EQ(*lolOpt, EPP_LOL_4_OCTETS);
}

/** @test Verify headerLengthFromLoL returns correct header sizes. */
TEST(EppViewerTest, HeaderLengthFromLoL) {
  EXPECT_EQ(PacketViewer::headerLengthFromLoL(EPP_LOL_IDLE), EPP_HEADER_1_OCTET);
  EXPECT_EQ(PacketViewer::headerLengthFromLoL(EPP_LOL_1_OCTET), EPP_HEADER_2_OCTET);
  EXPECT_EQ(PacketViewer::headerLengthFromLoL(EPP_LOL_2_OCTETS), EPP_HEADER_4_OCTET);
  EXPECT_EQ(PacketViewer::headerLengthFromLoL(EPP_LOL_4_OCTETS), EPP_HEADER_8_OCTET);
  EXPECT_EQ(PacketViewer::headerLengthFromLoL(0xFF), 0U); // Invalid
}

/* ----------------------------- ToString Tests ------------------------------ */

/** @test Verify toString for ValidationError. */
TEST(EppViewerTest, ValidationErrorToString) {
  EXPECT_STREQ(toString(ValidationError::OK), "OK");
  EXPECT_STREQ(toString(ValidationError::PACKET_TOO_SMALL), "PACKET_TOO_SMALL");
  EXPECT_STREQ(toString(ValidationError::INVALID_VERSION), "INVALID_VERSION");
  EXPECT_STREQ(toString(ValidationError::INVALID_LOL), "INVALID_LOL");
  EXPECT_STREQ(toString(ValidationError::LENGTH_MISMATCH), "LENGTH_MISMATCH");
  EXPECT_STREQ(toString(ValidationError::LENGTH_OVER_MAX), "LENGTH_OVER_MAX");
  EXPECT_STREQ(toString(ValidationError::HEADER_INCOMPLETE), "HEADER_INCOMPLETE");
}
