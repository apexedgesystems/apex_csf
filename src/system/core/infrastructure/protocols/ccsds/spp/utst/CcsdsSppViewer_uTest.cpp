/**
 * @file CcsdsSppViewer_uTest.cpp
 * @brief Unit tests for CCSDS SPP PacketViewer.
 *
 * Coverage:
 *  - No secondary header + payload slicing
 *  - With secondary header (optional config) + payload slicing
 *  - Insufficient packet size (factory rejects)
 *  - Missing/short secondary header when flag set (factory rejects)
 *  - No-secondary flag → empty secondary view
 *  - PD==0 (1-byte data field) accepted
 *  - secHdrLen > (PD+1) rejected
 *  - Fast-path helper (peekAPID with size validation)
 *  - Sequence gap detection (14-bit wraparound)
 *  - Detailed validation error codes
 *
 * Reference: CCSDS 133.0-B-2 Section 4 (Protocol Specification)
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppCommonDefs.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppViewer.hpp"

#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

using protocols::ccsds::spp::PacketViewer;
using protocols::ccsds::spp::SecondaryHeaderConfig;
using protocols::ccsds::spp::ValidationError;

/**
 * @brief Helper to assemble a packet: primary (6B) + optional secondary + payload.
 */
static std::vector<std::uint8_t> createPacket(const std::vector<std::uint8_t>& primaryHeader,
                                              const std::vector<std::uint8_t>& secHeader,
                                              const std::vector<std::uint8_t>& payload) {
  std::vector<std::uint8_t> packet;
  packet.reserve(primaryHeader.size() + secHeader.size() + payload.size());
  packet.insert(packet.end(), primaryHeader.begin(), primaryHeader.end());
  packet.insert(packet.end(), secHeader.begin(), secHeader.end());
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

/**
 * @brief Build a CCSDS SPP primary header (6 octets) from field values.
 *
 * Per CCSDS 133.0-B-2 Section 4.1.3:
 *  - Byte 0: version[7:5] | type[4] | secHdr[3] | APID[10:8]
 *  - Byte 1: APID[7:0]
 *  - Byte 2: seqFlags[7:6] | seqCount[13:8]
 *  - Byte 3: seqCount[7:0]
 *  - Bytes 4-5: Packet Data Length (big-endian) = (secondary + payload) - 1
 */
static std::vector<std::uint8_t>
buildPrimaryHeader(std::uint8_t version, bool type, bool secHdrFlag, std::uint16_t apid,
                   std::uint8_t seqFlags, std::uint16_t seqCount,
                   std::uint16_t dataFieldLength /* sec + payload */) {
  using namespace protocols::ccsds::spp;

  std::vector<std::uint8_t> h(6, 0);
  const std::uint16_t APID_FIELD = static_cast<std::uint16_t>(apid & SPP_APID_MASK);

  // Byte 0
  h[0] = static_cast<std::uint8_t>(
      ((version & SPP_VERSION_MASK) << SPP_VERSION_SHIFT) | ((type ? 1u : 0u) << SPP_TYPE_SHIFT) |
      ((secHdrFlag ? 1u : 0u) << SPP_SECHDR_SHIFT) | ((APID_FIELD >> 8) & SPP_APID_UPPER_MASK3));
  // Byte 1
  h[1] = static_cast<std::uint8_t>(APID_FIELD & 0xFFu);
  // Byte 2
  h[2] = static_cast<std::uint8_t>(((seqFlags & SPP_SEQFLAGS_MASK) << SPP_SEQFLAGS_SHIFT) |
                                   ((seqCount >> 8) & SPP_SEQCOUNT_UPPER6_MASK));
  // Byte 3
  h[3] = static_cast<std::uint8_t>(seqCount & 0xFFu);
  // Bytes 4-5: PD length = (dataFieldLength - 1)
  const std::uint16_t PD_LENGTH = static_cast<std::uint16_t>(dataFieldLength - 1u);
  h[4] = static_cast<std::uint8_t>((PD_LENGTH >> 8) & 0xFFu);
  h[5] = static_cast<std::uint8_t>(PD_LENGTH & 0xFFu);
  return h;
}

/* ========================== Basic Viewing Tests ============================ */

/**
 * @test Viewer with no secondary header present.
 *
 * Per CCSDS 133.0-B-2 Section 4.1.4.3.2: "The User Data Field shall be mandatory
 * if a Packet Secondary Header is not present."
 */
TEST(PacketViewerTest, NoSecondaryHeader) {
  using namespace protocols::ccsds::spp;

  // Arrange
  std::vector<std::uint8_t> payload{0x11, 0x22, 0x33, 0x44};
  auto primary = buildPrimaryHeader(/*version*/ 0, /*type*/ true, /*secHdr*/ false,
                                    /*apid*/ 0x300, /*seqFlags*/ 2, /*seqCount*/ 0x0101,
                                    /*dataFieldLength*/ static_cast<std::uint16_t>(payload.size()));
  auto packet = createPacket(primary, {}, payload);

  // Total expected length = PD + 7 = (4-1) + 7 = 10
  EXPECT_EQ(packet.size(), 10u);

  // Act
  SecondaryHeaderConfig cfg{}; // No secondary header
  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);

  // Assert
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;

  // Primary header fields (method calls for parse-on-demand)
  EXPECT_EQ(view.pri.version(), 0);
  EXPECT_EQ(view.pri.type(), true);
  EXPECT_EQ(view.pri.hasSecondaryHeader(), false);
  EXPECT_EQ(view.pri.apid(), 0x300);
  EXPECT_EQ(view.pri.sequenceFlags(), 2);
  EXPECT_EQ(view.pri.sequenceCount(), 0x0101);
  EXPECT_EQ(view.pri.packetDataLength(), 3u);

  // Secondary header must be empty
  EXPECT_TRUE(view.sec.raw.empty());
  EXPECT_FALSE(view.sec.hasTimeCode());
  EXPECT_FALSE(view.sec.hasAncillaryData());

  // Payload
  auto userData = view.payload();
  ASSERT_EQ(userData.size(), payload.size());
  for (std::size_t i = 0; i < payload.size(); ++i) {
    EXPECT_EQ(userData[i], payload[i]);
  }

  // Raw packet bytes
  ASSERT_EQ(view.raw.size(), packet.size());
  for (std::size_t i = 0; i < packet.size(); ++i) {
    EXPECT_EQ(view.raw[i], packet[i]);
  }
}

/**
 * @test Viewer with a secondary header present.
 *
 * Per CCSDS 133.0-B-2 Section 4.1.4.2: Secondary header format is mission-specific.
 */
TEST(PacketViewerTest, WithSecondaryHeader) {
  using namespace protocols::ccsds::spp;

  // Arrange
  std::vector<std::uint8_t> secHeader{0xAA, 0xBB, 0xCC};
  std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF, 0xFF};
  const std::uint16_t DATA_FIELD_LENGTH =
      static_cast<std::uint16_t>(secHeader.size() + payload.size());
  auto primary = buildPrimaryHeader(/*version*/ 0, /*type*/ false, /*secHdr*/ true,
                                    /*apid*/ 0x1F, /*seqFlags*/ 1, /*seqCount*/ 0x0055,
                                    /*dataFieldLength*/ DATA_FIELD_LENGTH);
  auto packet = createPacket(primary, secHeader, payload);

  // Total expected length = PD + 7 = (8-1) + 7 = 14
  EXPECT_EQ(packet.size(), 14u);

  // Act
  SecondaryHeaderConfig cfg{};
  cfg.totalLength = secHeader.size();
  // No time code for this test (treated as opaque ancillary data)

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);

  // Assert
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;

  EXPECT_EQ(view.pri.version(), 0);
  EXPECT_FALSE(view.pri.type());
  EXPECT_TRUE(view.pri.hasSecondaryHeader());
  EXPECT_EQ(view.pri.apid(), 0x1F);
  EXPECT_EQ(view.pri.sequenceFlags(), 1);
  EXPECT_EQ(view.pri.sequenceCount(), 0x0055);
  EXPECT_EQ(view.pri.packetDataLength(), 7u);

  // Secondary header raw bytes
  ASSERT_EQ(view.sec.raw.size(), secHeader.size());
  for (std::size_t i = 0; i < secHeader.size(); ++i) {
    EXPECT_EQ(view.sec.raw[i], secHeader[i]);
  }

  // Since no time code configured, all sec header bytes are ancillary data
  EXPECT_FALSE(view.sec.hasTimeCode());
  EXPECT_TRUE(view.sec.hasAncillaryData());
  ASSERT_EQ(view.sec.ancillaryData.size(), secHeader.size());

  // Payload
  auto userData = view.payload();
  ASSERT_EQ(userData.size(), payload.size());
  for (std::size_t i = 0; i < payload.size(); ++i) {
    EXPECT_EQ(userData[i], payload[i]);
  }
}

/* ========================== Validation Tests =============================== */

/**
 * @test Incomplete packet (< 6 bytes) is rejected by the factory.
 *
 * Per CCSDS 133.0-B-2 Section 4.1.3: Primary header is 6 octets.
 */
TEST(PacketViewerTest, InsufficientPacketSize) {
  std::vector<std::uint8_t> shortPacket{0x00, 0x01, 0x02, 0x03, 0x04};
  SecondaryHeaderConfig cfg{};

  auto viewOpt =
      PacketViewer::create(apex::compat::bytes_span{shortPacket.data(), shortPacket.size()}, cfg);
  EXPECT_FALSE(viewOpt.has_value());

  // Check detailed error code
  auto err =
      PacketViewer::validate(apex::compat::bytes_span{shortPacket.data(), shortPacket.size()}, cfg);
  EXPECT_EQ(err, ValidationError::PACKET_TOO_SMALL);
}

/**
 * @test Secondary header indicated but incomplete data → rejected.
 */
TEST(PacketViewerTest, MissingSecondaryHeader) {
  // Arrange
  std::vector<std::uint8_t> payload{0x55, 0x66};
  const std::uint16_t EXPECTED_SEC_LEN = 4;
  const std::uint16_t DATA_FIELD_LENGTH =
      static_cast<std::uint16_t>(EXPECTED_SEC_LEN + payload.size());
  auto primary = buildPrimaryHeader(/*version*/ 0, /*type*/ true, /*secHdr*/ true,
                                    /*apid*/ 0x200, /*seqFlags*/ 0, /*seqCount*/ 0x0001,
                                    /*dataFieldLength*/ DATA_FIELD_LENGTH);

  // Provide only 2 bytes of the supposed 4-byte secondary header
  std::vector<std::uint8_t> secHeader{0xAA, 0xBB};
  auto packet = createPacket(primary, secHeader, payload);

  // Act
  SecondaryHeaderConfig cfg{};
  cfg.totalLength = EXPECTED_SEC_LEN; // Claim 4 bytes but packet only has 2

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);

  // Assert
  EXPECT_FALSE(viewOpt.has_value());

  auto err = PacketViewer::validate(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);
  EXPECT_EQ(err, ValidationError::PD_LENGTH_MISMATCH);
}

/**
 * @test No secondary header flag → secondary view is empty, payload follows primary header.
 */
TEST(PacketViewerTest, NoSecondaryHeaderFlag) {
  // Arrange
  std::vector<std::uint8_t> payload{0xAA, 0xBB, 0xCC};
  auto primary = buildPrimaryHeader(/*version*/ 0, /*type*/ false, /*secHdr*/ false,
                                    /*apid*/ 0x100, /*seqFlags*/ 0, /*seqCount*/ 0x0002,
                                    /*dataFieldLength*/ static_cast<std::uint16_t>(payload.size()));
  auto packet = createPacket(primary, {}, payload);

  // Act
  SecondaryHeaderConfig cfg{}; // totalLength = 0
  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);

  // Assert
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;

  EXPECT_FALSE(view.pri.hasSecondaryHeader());
  EXPECT_TRUE(view.sec.raw.empty());

  auto userData = view.payload();
  ASSERT_EQ(userData.size(), payload.size());
  for (std::size_t i = 0; i < payload.size(); ++i) {
    EXPECT_EQ(userData[i], payload[i]);
  }
}

/**
 * @test PD==0 (1-byte data field) is accepted.
 * Total length should be PD + 7 = 0 + 7 = 7.
 *
 * Per CCSDS 133.0-B-2 Section 4.1.3.3.4: PD = (data_field_length - 1)
 */
TEST(PacketViewerTest, PDZeroOneBytePayload) {
  // Arrange: no secondary header, 1-byte payload
  std::vector<std::uint8_t> payload{0xEE};
  auto primary = buildPrimaryHeader(/*version*/ 0, /*type*/ false, /*secHdr*/ false,
                                    /*apid*/ 0x001, /*seqFlags*/ 0, /*seqCount*/ 0x0000,
                                    /*dataFieldLength*/ static_cast<std::uint16_t>(payload.size()));
  auto packet = createPacket(primary, {}, payload);
  EXPECT_EQ(packet.size(), 7u);

  // Act
  SecondaryHeaderConfig cfg{};
  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);

  // Assert
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;
  EXPECT_EQ(view.pri.packetDataLength(), 0u);

  auto userData = view.payload();
  ASSERT_EQ(userData.size(), 1u);
  EXPECT_EQ(userData[0], 0xEE);
}

/**
 * @test Provided secHdrLen greater than PD+1 is rejected.
 * (Factory checks secHdrLen <= PD + 1.)
 */
TEST(PacketViewerTest, SecondaryHeaderOversized) {
  // Arrange: primary says PD=2 (so PD+1=3), but we claim secHdrLen=5
  std::vector<std::uint8_t> payload{0x01, 0x02, 0x03};
  const std::uint16_t DATA_FIELD_LENGTH = static_cast<std::uint16_t>(payload.size()); // 3 -> PD=2
  auto primary = buildPrimaryHeader(/*version*/ 0, /*type*/ false, /*secHdr*/ true,
                                    /*apid*/ 0x002, /*seqFlags*/ 0, /*seqCount*/ 0x0000,
                                    /*dataFieldLength*/ DATA_FIELD_LENGTH);
  auto packet = createPacket(primary, {}, payload);

  // Act: claim secondary header is 5 bytes when PD+1 = 3
  SecondaryHeaderConfig cfg{};
  cfg.totalLength = 5;

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);

  // Assert
  EXPECT_FALSE(viewOpt.has_value());

  auto err = PacketViewer::validate(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);
  EXPECT_EQ(err, ValidationError::SECONDARY_OVERSIZE);
}

/* ========================== Fast-Path Helpers ============================== */

/**
 * @test peekAPID extracts APID without full parsing.
 *
 * Per CCSDS 133.0-B-2 Section 4.1.3.2: APID is 11 bits in octets 0-1.
 */
TEST(PacketViewerTest, PeekAPID) {
  // Arrange: Valid packet
  std::vector<std::uint8_t> payload{0x01, 0x02};
  const std::uint16_t EXPECTED_APID = 0x3FF; // 11 bits all set
  auto primary = buildPrimaryHeader(/*version*/ 0, /*type*/ false, /*secHdr*/ false,
                                    /*apid*/ EXPECTED_APID, /*seqFlags*/ 0, /*seqCount*/ 0,
                                    /*dataFieldLength*/ static_cast<std::uint16_t>(payload.size()));
  auto packet = createPacket(primary, {}, payload);

  // Act & Assert: Valid packet returns APID
  auto apidOpt = PacketViewer::peekAPID(apex::compat::bytes_span{packet.data(), packet.size()});
  ASSERT_TRUE(apidOpt.has_value());
  EXPECT_EQ(*apidOpt, EXPECTED_APID);

  // Too small: Should return nullopt
  std::vector<std::uint8_t> tooSmall{0x00, 0x01, 0x02}; // Only 3 bytes
  auto apidOpt2 =
      PacketViewer::peekAPID(apex::compat::bytes_span{tooSmall.data(), tooSmall.size()});
  EXPECT_FALSE(apidOpt2.has_value());
}

/* ========================== Sequence Gap Detection ========================= */

/**
 * @test hasSequenceGap detects missing packets.
 *
 * Per CCSDS 133.0-B-2 Section 4.1.3.3.2: Sequence count increments modulo-16384.
 */
TEST(PacketViewerTest, SequenceGapDetection) {
  std::vector<std::uint8_t> payload{0x01};
  SecondaryHeaderConfig cfg{};

  // Packet 1: seqCount = 100
  auto primary1 = buildPrimaryHeader(0, false, false, 0x100, 0, 100, payload.size());
  auto packet1 = createPacket(primary1, {}, payload);
  auto view1 = PacketViewer::create(apex::compat::bytes_span{packet1.data(), packet1.size()}, cfg);
  ASSERT_TRUE(view1.has_value());

  // Packet 2: seqCount = 101 (no gap)
  auto primary2 = buildPrimaryHeader(0, false, false, 0x100, 0, 101, payload.size());
  auto packet2 = createPacket(primary2, {}, payload);
  auto view2 = PacketViewer::create(apex::compat::bytes_span{packet2.data(), packet2.size()}, cfg);
  ASSERT_TRUE(view2.has_value());
  EXPECT_FALSE(view2->hasSequenceGap(100)); // Expected 101, got 101

  // Packet 3: seqCount = 105 (gap of 3 packets)
  auto primary3 = buildPrimaryHeader(0, false, false, 0x100, 0, 105, payload.size());
  auto packet3 = createPacket(primary3, {}, payload);
  auto view3 = PacketViewer::create(apex::compat::bytes_span{packet3.data(), packet3.size()}, cfg);
  ASSERT_TRUE(view3.has_value());
  EXPECT_TRUE(view3->hasSequenceGap(101)); // Expected 102, got 105 -> gap!
}

/**
 * @test Sequence count wraparound (14-bit modulo).
 */
TEST(PacketViewerTest, SequenceCountWraparound) {
  using namespace protocols::ccsds::spp;

  std::vector<std::uint8_t> payload{0x01};
  SecondaryHeaderConfig cfg{};

  // Packet at max sequence count (0x3FFF = 16383)
  auto primary1 = buildPrimaryHeader(0, false, false, 0x100, 0, 0x3FFF, payload.size());
  auto packet1 = createPacket(primary1, {}, payload);
  auto view1 = PacketViewer::create(apex::compat::bytes_span{packet1.data(), packet1.size()}, cfg);
  ASSERT_TRUE(view1.has_value());
  EXPECT_EQ(view1->pri.sequenceCount(), 0x3FFF);

  // Next packet wraps to 0
  auto primary2 = buildPrimaryHeader(0, false, false, 0x100, 0, 0, payload.size());
  auto packet2 = createPacket(primary2, {}, payload);
  auto view2 = PacketViewer::create(apex::compat::bytes_span{packet2.data(), packet2.size()}, cfg);
  ASSERT_TRUE(view2.has_value());
  EXPECT_FALSE(view2->hasSequenceGap(0x3FFF)); // This is expected wraparound, not a gap
}