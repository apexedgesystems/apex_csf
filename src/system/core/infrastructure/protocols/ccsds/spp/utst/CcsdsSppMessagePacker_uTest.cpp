/**
 * @file CcsdsSppMessagePacker_uTest.cpp
 * @brief Unit tests for immutable CCSDS SPP message packer.
 *
 * Coverage:
 *  - Create without secondary header
 *  - Create with secondary header (timeCode + ancillary)
 *  - Empty user data rejected
 *  - Packet length limit enforced
 *  - Minimal payload (PD==0) accepted
 *  - Secondary header: only timeCode / only ancillary
 *  - Invalid primary-header field values rejected
 */

#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppCommonDefs.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppMessagePacker.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppViewer.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

using protocols::ccsds::spp::MAX_SPP_PACKET_LENGTH;
using protocols::ccsds::spp::PrimaryHeaderView;
using protocols::ccsds::spp::SPP_HDR_SIZE_BYTES;
using protocols::ccsds::spp::SppMsg;
using protocols::ccsds::spp::SppMsgDefault;

/**
 * @test Create without secondary header.
 * Layout: [Primary(6)] [User]
 */
TEST(SppMsgPackerTest, CreateWithoutSecondaryHeader) {
  // Arrange
  std::vector<std::uint8_t> userData{0xFA, 0xFB, 0xFC, 0xFD};

  // Act
  auto msgOpt = SppMsgDefault::create(
      /*version*/ 1,
      /*type*/ true, // telecommand
      /*apid*/ 0x300,
      /*seqFlags*/ 2,
      /*seqCount*/ 100,
      /*timeCode*/ apex::compat::bytes_span{},
      /*ancillary*/ apex::compat::bytes_span{},
      /*userData*/ apex::compat::bytes_span{userData.data(), userData.size()});

  // Assert
  ASSERT_TRUE(msgOpt.has_value());
  const auto& MSG = *msgOpt;

  const auto PACKET = MSG.data();
  ASSERT_EQ(PACKET.size(), SPP_HDR_SIZE_BYTES + userData.size());

  // Parse primary header using the viewer's primary view.
  PrimaryHeaderView ph{apex::compat::bytes_span{PACKET.data(), SPP_HDR_SIZE_BYTES}};
  EXPECT_EQ(ph.version(), 1);
  EXPECT_TRUE(ph.type());
  EXPECT_FALSE(ph.hasSecondaryHeader());
  EXPECT_EQ(ph.apid(), 0x300);
  EXPECT_EQ(ph.sequenceFlags(), 2);
  EXPECT_EQ(ph.sequenceCount(), 100);
  EXPECT_EQ(ph.packetDataLength(), static_cast<std::uint16_t>(userData.size() - 1));

  // Payload immediately follows primary header.
  std::vector<std::uint8_t> extracted(PACKET.data() + SPP_HDR_SIZE_BYTES,
                                      PACKET.data() + PACKET.size());
  EXPECT_EQ(extracted, userData);
}

/**
 * @test Create with secondary header (timeCode + ancillary).
 * Layout: [Primary(6)] [Secondary(5)] [User(3)]
 */
TEST(SppMsgPackerTest, CreateWithSecondaryHeader) {
  // Arrange
  std::vector<std::uint8_t> timeCode{0x11, 0x22, 0x33};
  std::vector<std::uint8_t> ancillary{0x44, 0x55};
  std::vector<std::uint8_t> userData{0xAA, 0xBB, 0xCC};

  const std::size_t SEC_LEN = timeCode.size() + ancillary.size();
  const std::size_t EXPECTED_LEN = SPP_HDR_SIZE_BYTES + SEC_LEN + userData.size();
  const std::uint16_t EXPECTED_PD = static_cast<std::uint16_t>((SEC_LEN + userData.size()) - 1u);

  // Act
  auto msgOpt = SppMsgDefault::create(
      /*version*/ 2,
      /*type*/ false, // telemetry
      /*apid*/ 0x1FF,
      /*seqFlags*/ 3,
      /*seqCount*/ 200,
      /*timeCode*/ apex::compat::bytes_span{timeCode.data(), timeCode.size()},
      /*ancillary*/ apex::compat::bytes_span{ancillary.data(), ancillary.size()},
      /*userData*/ apex::compat::bytes_span{userData.data(), userData.size()});

  // Assert
  ASSERT_TRUE(msgOpt.has_value());
  const auto& MSG = *msgOpt;

  const auto PACKET = MSG.data();
  ASSERT_EQ(PACKET.size(), EXPECTED_LEN);

  // Primary
  PrimaryHeaderView ph{apex::compat::bytes_span{PACKET.data(), SPP_HDR_SIZE_BYTES}};
  EXPECT_EQ(ph.version(), 2);
  EXPECT_FALSE(ph.type());
  EXPECT_TRUE(ph.hasSecondaryHeader());
  EXPECT_EQ(ph.apid(), 0x1FF);
  EXPECT_EQ(ph.sequenceFlags(), 3);
  EXPECT_EQ(ph.sequenceCount(), 200);
  EXPECT_EQ(ph.packetDataLength(), EXPECTED_PD);

  // Secondary bytes
  std::vector<std::uint8_t> secOut(PACKET.data() + SPP_HDR_SIZE_BYTES,
                                   PACKET.data() + SPP_HDR_SIZE_BYTES + SEC_LEN);
  std::vector<std::uint8_t> secExpected;
  secExpected.reserve(SEC_LEN);
  secExpected.insert(secExpected.end(), timeCode.begin(), timeCode.end());
  secExpected.insert(secExpected.end(), ancillary.begin(), ancillary.end());
  EXPECT_EQ(secOut, secExpected);

  // User bytes
  std::vector<std::uint8_t> userOut(PACKET.data() + SPP_HDR_SIZE_BYTES + SEC_LEN,
                                    PACKET.data() + PACKET.size());
  EXPECT_EQ(userOut, userData);
}

/**
 * @test Empty user data is rejected.
 */
TEST(SppMsgPackerTest, CreateFailsEmptyUserData) {
  auto msgOpt = SppMsgDefault::create(1, true, 0x100, 0, 10, apex::compat::bytes_span{},
                                      apex::compat::bytes_span{}, apex::compat::bytes_span{});
  EXPECT_FALSE(msgOpt.has_value());
}

/**
 * @test Packet length limit enforced (MAX_SPP_PACKET_LENGTH).
 */
TEST(SppMsgPackerTest, PacketLengthExceedsMax) {
  // Choose user size so that 6 + user > MAX
  const std::size_t TOO_BIG_USER = (MAX_SPP_PACKET_LENGTH > SPP_HDR_SIZE_BYTES)
                                       ? (MAX_SPP_PACKET_LENGTH - SPP_HDR_SIZE_BYTES + 1)
                                       : (static_cast<std::size_t>(1));

  std::vector<std::uint8_t> userData(TOO_BIG_USER, 0xAB);

  auto msgOpt = SppMsgDefault::create(1, false, 0x123, 1, 20, apex::compat::bytes_span{},
                                      apex::compat::bytes_span{},
                                      apex::compat::bytes_span{userData.data(), userData.size()});

  EXPECT_FALSE(msgOpt.has_value());
}

/**
 * @test Minimal payload (1 byte -> PD == 0) without secondary header.
 * Layout: [Primary(6)] [User(1)]
 */
TEST(SppMsgPackerTest, CreateMinimalPayloadNoSecondary) {
  // Arrange
  std::vector<std::uint8_t> userData{0xEE};

  // Act
  auto msgOpt = SppMsgDefault::create(0, false, 0x001, 0, 0, apex::compat::bytes_span{},
                                      apex::compat::bytes_span{},
                                      apex::compat::bytes_span{userData.data(), userData.size()});

  // Assert
  ASSERT_TRUE(msgOpt.has_value());
  const auto PACKET = msgOpt->data();
  ASSERT_EQ(PACKET.size(), SPP_HDR_SIZE_BYTES + 1u);

  PrimaryHeaderView ph{apex::compat::bytes_span{PACKET.data(), SPP_HDR_SIZE_BYTES}};
  EXPECT_EQ(ph.packetDataLength(), 0u);

  // Check payload byte
  EXPECT_EQ(PACKET[SPP_HDR_SIZE_BYTES + 0], 0xEE);
}

/**
 * @test Secondary header present with only Time Code (no ancillary).
 * Layout: [Primary(6)] [TimeCode(3)] [User(2)]
 */
TEST(SppMsgPackerTest, CreateWithOnlyTimeCode) {
  // Arrange
  std::vector<std::uint8_t> timeCode{0x10, 0x20, 0x30};
  std::vector<std::uint8_t> userData{0xA0, 0xB0};
  const std::size_t SEC_LEN = timeCode.size();

  // Act
  auto msgOpt = SppMsgDefault::create(
      0, true, 0x077, 1, 5, apex::compat::bytes_span{timeCode.data(), timeCode.size()},
      apex::compat::bytes_span{}, apex::compat::bytes_span{userData.data(), userData.size()});

  // Assert
  ASSERT_TRUE(msgOpt.has_value());
  const auto PACKET = msgOpt->data();
  ASSERT_EQ(PACKET.size(), SPP_HDR_SIZE_BYTES + SEC_LEN + userData.size());

  PrimaryHeaderView ph{apex::compat::bytes_span{PACKET.data(), SPP_HDR_SIZE_BYTES}};
  EXPECT_TRUE(ph.hasSecondaryHeader());
  EXPECT_EQ(ph.packetDataLength(), static_cast<std::uint16_t>((SEC_LEN + userData.size()) - 1u));

  // Check secondary region
  std::vector<std::uint8_t> secOut(PACKET.data() + SPP_HDR_SIZE_BYTES,
                                   PACKET.data() + SPP_HDR_SIZE_BYTES + SEC_LEN);
  EXPECT_EQ(secOut, timeCode);
}

/**
 * @test Secondary header present with only Ancillary (no time code).
 * Layout: [Primary(6)] [Ancillary(4)] [User(1)]
 */
TEST(SppMsgPackerTest, CreateWithOnlyAncillary) {
  // Arrange
  std::vector<std::uint8_t> ancillary{0xDE, 0xAD, 0xBE, 0xEF};
  std::vector<std::uint8_t> userData{0x7F};
  const std::size_t SEC_LEN = ancillary.size();

  // Act
  auto msgOpt = SppMsgDefault::create(3, false, 0x002, 2, 9, apex::compat::bytes_span{},
                                      apex::compat::bytes_span{ancillary.data(), ancillary.size()},
                                      apex::compat::bytes_span{userData.data(), userData.size()});

  // Assert
  ASSERT_TRUE(msgOpt.has_value());
  const auto PACKET = msgOpt->data();
  ASSERT_EQ(PACKET.size(), SPP_HDR_SIZE_BYTES + SEC_LEN + userData.size());

  PrimaryHeaderView ph{apex::compat::bytes_span{PACKET.data(), SPP_HDR_SIZE_BYTES}};
  EXPECT_TRUE(ph.hasSecondaryHeader());
  EXPECT_EQ(ph.packetDataLength(), static_cast<std::uint16_t>((SEC_LEN + userData.size()) - 1u));

  // Check secondary region
  std::vector<std::uint8_t> secOut(PACKET.data() + SPP_HDR_SIZE_BYTES,
                                   PACKET.data() + SPP_HDR_SIZE_BYTES + SEC_LEN);
  EXPECT_EQ(secOut, ancillary);
}

/**
 * @test Invalid primary header field values are rejected by the builder used in SppMsg::create.
 * We check each category briefly: version, APID, seqFlags, seqCount.
 */
TEST(SppMsgPackerTest, InvalidFieldValuesRejected) {
  // Common valid buffers for time/anc/user
  std::vector<std::uint8_t> user{0x01};

  // Version > 7
  auto vBad = SppMsgDefault::create(
      /*version*/ 8, /*type*/ false, /*apid*/ 0x001, /*seqFlags*/ 0, /*seqCount*/ 0,
      apex::compat::bytes_span{}, apex::compat::bytes_span{},
      apex::compat::bytes_span{user.data(), user.size()});
  EXPECT_FALSE(vBad.has_value());

  // APID > 0x7FF
  auto apidBad = SppMsgDefault::create(0, false, /*apid*/ 0x1800, 0, 0, apex::compat::bytes_span{},
                                       apex::compat::bytes_span{},
                                       apex::compat::bytes_span{user.data(), user.size()});
  EXPECT_FALSE(apidBad.has_value());

  // seqFlags > 3
  auto flagsBad = SppMsgDefault::create(0, false, 0x001, /*seqFlags*/ 4, 0,
                                        apex::compat::bytes_span{}, apex::compat::bytes_span{},
                                        apex::compat::bytes_span{user.data(), user.size()});
  EXPECT_FALSE(flagsBad.has_value());

  // seqCount > 0x3FFF
  auto countBad = SppMsgDefault::create(0, false, 0x001, 0, /*seqCount*/ 0x4000,
                                        apex::compat::bytes_span{}, apex::compat::bytes_span{},
                                        apex::compat::bytes_span{user.data(), user.size()});
  EXPECT_FALSE(countBad.has_value());
}

/**
 * @test Secondary-only packet: timeCode present, empty userData -> accepted.
 */
TEST(SppMsgPackerTest, SecondaryOnlyNoUserDataAccepted) {
  using namespace protocols::ccsds::spp;

  std::vector<std::uint8_t> timeCode{0x10, 0x20, 0x30, 0x40};
  apex::compat::bytes_span tc{timeCode.data(), timeCode.size()};
  apex::compat::bytes_span empty{};

  auto msgOpt = SppMsgDefault::create(
      /*version*/ 0,
      /*type*/ false, // TM
      /*apid*/ 0x005,
      /*seqFlags*/ 0,
      /*seqCount*/ 1,
      /*timeCode*/ tc,
      /*ancillary*/ empty,
      /*userData*/ empty);

  ASSERT_TRUE(msgOpt.has_value());
  const auto PKT = msgOpt->data();

  // Total = 6 + timeCode.size()
  ASSERT_EQ(PKT.size(), SPP_HDR_SIZE_BYTES + timeCode.size());

  PrimaryHeaderView ph{apex::compat::bytes_span{PKT.data(), SPP_HDR_SIZE_BYTES}};
  EXPECT_TRUE(ph.hasSecondaryHeader());
  EXPECT_EQ(ph.packetDataLength(), static_cast<std::uint16_t>(timeCode.size() - 1));

  // Secondary bytes match
  std::vector<std::uint8_t> secOut(PKT.data() + SPP_HDR_SIZE_BYTES,
                                   PKT.data() + SPP_HDR_SIZE_BYTES + timeCode.size());
  EXPECT_EQ(secOut, timeCode);
}

/**
 * @test packPacket() free function writes directly to caller buffer.
 */
TEST(SppMsgPackerTest, PackPacketFreeFunction) {
  using namespace protocols::ccsds::spp;

  std::vector<std::uint8_t> userData{0xCA, 0xFE, 0xBA, 0xBE};
  std::array<std::uint8_t, 64> buffer{};
  std::size_t written = 0;

  bool ok = packPacket(/*version*/ 1, /*type*/ true, /*apid*/ 0x123, /*seqFlags*/ 2,
                       /*seqCount*/ 42, apex::compat::bytes_span{}, apex::compat::bytes_span{},
                       apex::compat::bytes_span{userData.data(), userData.size()}, buffer.data(),
                       buffer.size(), written);

  ASSERT_TRUE(ok);
  EXPECT_EQ(written, SPP_HDR_SIZE_BYTES + userData.size());

  // Verify header
  PrimaryHeaderView ph{apex::compat::bytes_span{buffer.data(), SPP_HDR_SIZE_BYTES}};
  EXPECT_EQ(ph.version(), 1);
  EXPECT_TRUE(ph.type());
  EXPECT_EQ(ph.apid(), 0x123);
  EXPECT_EQ(ph.sequenceFlags(), 2);
  EXPECT_EQ(ph.sequenceCount(), 42);
  EXPECT_EQ(ph.packetDataLength(), static_cast<std::uint16_t>(userData.size() - 1));
}

/**
 * @test packPacket() fails when buffer too small.
 */
TEST(SppMsgPackerTest, PackPacketBufferTooSmall) {
  using namespace protocols::ccsds::spp;

  std::vector<std::uint8_t> userData{0x01, 0x02, 0x03};
  std::array<std::uint8_t, 5> smallBuffer{}; // Too small for 6 + 3 = 9 bytes
  std::size_t written = 0;

  bool ok =
      packPacket(0, false, 0x001, 0, 0, apex::compat::bytes_span{}, apex::compat::bytes_span{},
                 apex::compat::bytes_span{userData.data(), userData.size()}, smallBuffer.data(),
                 smallBuffer.size(), written);

  EXPECT_FALSE(ok);
}

/**
 * @test requiredPacketSize() computes correct sizes.
 */
TEST(SppMsgPackerTest, RequiredPacketSize) {
  using namespace protocols::ccsds::spp;

  EXPECT_EQ(requiredPacketSize(0, 0, 0), 0u); // Invalid: empty data field
  EXPECT_EQ(requiredPacketSize(0, 0, 1), SPP_HDR_SIZE_BYTES + 1);
  EXPECT_EQ(requiredPacketSize(4, 0, 3), SPP_HDR_SIZE_BYTES + 4 + 3);
  EXPECT_EQ(requiredPacketSize(0, 8, 0), SPP_HDR_SIZE_BYTES + 8);
  EXPECT_EQ(requiredPacketSize(4, 4, 4), SPP_HDR_SIZE_BYTES + 12);
}

/**
 * @test SppSecondaryHeader template with fixed capacity.
 */
TEST(SppSecondaryHeaderTest, FixedCapacityEnforced) {
  using namespace protocols::ccsds::spp;

  // Small capacity (4 bytes time code, 8 bytes ancillary)
  using SmallSecHdr = SppSecondaryHeader<4, 8>;

  // Should succeed with data within capacity
  std::array<std::uint8_t, 4> tc{0x11, 0x22, 0x33, 0x44};
  std::array<std::uint8_t, 6> anc{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

  auto hdrOpt = SmallSecHdr::fromFields(apex::compat::bytes_span{tc.data(), tc.size()},
                                        apex::compat::bytes_span{anc.data(), anc.size()});
  ASSERT_TRUE(hdrOpt.has_value());
  EXPECT_EQ(hdrOpt->length(), tc.size() + anc.size());
  EXPECT_TRUE(hdrOpt->hasTimeCode());
  EXPECT_TRUE(hdrOpt->hasAncillaryData());

  // Should fail with data exceeding capacity
  std::array<std::uint8_t, 5> tcTooBig{0x11, 0x22, 0x33, 0x44, 0x55};
  auto failOpt = SmallSecHdr::fromFields(apex::compat::bytes_span{tcTooBig.data(), tcTooBig.size()},
                                         apex::compat::bytes_span{});
  EXPECT_FALSE(failOpt.has_value());
}
