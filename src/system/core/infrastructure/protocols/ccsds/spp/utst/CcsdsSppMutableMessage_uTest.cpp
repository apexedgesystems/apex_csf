/**
 * @file CcsdsSppMutableMessage_uTest.cpp
 * @brief Unit tests for mutable CCSDS SPP message facade.
 *
 * Coverage:
 *  - Factory (single payload by ref) -> pack + packInto
 *  - Factory with secondary header (timeCode + ancillary)
 *  - Factory with payload span (apex::compat::rospan)
 *  - External mutation of payload and header reflected on pack
 *  - Zero-alloc writer packInto() parity with pack()
 *  - Multi-element span payload
 *  - Empty span rejected
 *  - Out-of-range header fields rejected via pack()
 *  - includeSecondary=true but empty data -> no secondary in final packet
 */

#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppCommonDefs.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppMutableMessage.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppViewer.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>

using protocols::ccsds::spp::MutableSppMessageFactory;
using protocols::ccsds::spp::MutableSppMessageT;
using protocols::ccsds::spp::MutableSppSecondaryHeader;
using protocols::ccsds::spp::MutableSppSecondaryHeaderDefault;
using protocols::ccsds::spp::PrimaryHeaderView;
using protocols::ccsds::spp::SPP_HDR_SIZE_BYTES;

/* Test payloads */
struct MyPayload1 {
  std::uint32_t sensorData;
};
struct MyPayload2 {
  std::uint16_t value;
};

/**
 * @test Create using payload instance (by reference). No secondary header.
 * Layout: [Primary(6)] [Payload]
 */
TEST(MutableSppMessageTest, FactorySinglePayloadByRef) {
  // Arrange
  MyPayload1 payload{0x12345678};

  // Act
  auto msgOpt = MutableSppMessageFactory::build<MyPayload1>(
      /*includeSecondary*/ false,
      /*version*/ 0,
      /*type*/ false, // TM
      /*apid*/ 0x0123,
      /*seqFlags*/ 1,
      /*seqCount*/ 42,
      /*payloadInstance*/ payload);

  // Assert
  ASSERT_TRUE(msgOpt.has_value());
  const auto PACKED_OPT = msgOpt->pack();
  ASSERT_TRUE(PACKED_OPT.has_value());
  const auto PACKET = PACKED_OPT->data();

  const std::size_t PAYLOAD_SIZE = sizeof(MyPayload1);
  ASSERT_EQ(PACKET.size(), SPP_HDR_SIZE_BYTES + PAYLOAD_SIZE);

  // PD = PAYLOAD_SIZE - 1 (no secondary)
  const std::uint16_t PD = static_cast<std::uint16_t>((PACKET[4] << 8) | PACKET[5]);
  EXPECT_EQ(PD, static_cast<std::uint16_t>(PAYLOAD_SIZE - 1));

  // Payload bytes match
  std::vector<std::uint8_t> extracted(PACKET.data() + SPP_HDR_SIZE_BYTES,
                                      PACKET.data() + PACKET.size());
  std::vector<std::uint8_t> expected(PAYLOAD_SIZE);
  std::memcpy(expected.data(), &payload, PAYLOAD_SIZE);
  EXPECT_EQ(extracted, expected);
}

/**
 * @test Create with secondary header (timeCode + ancillary).
 * Layout: [Primary(6)] [Secondary] [Payload]
 */
TEST(MutableSppMessageTest, FactoryWithSecondaryHeader) {
  // Arrange
  MyPayload2 payload{0xABCD};
  MutableSppSecondaryHeaderDefault sec;
  std::array<std::uint8_t, 3> tc{0x11, 0x22, 0x33};
  std::array<std::uint8_t, 2> anc{0x44, 0x55};
  ASSERT_TRUE(sec.setTimeCode(apex::compat::bytes_span{tc.data(), tc.size()}));
  ASSERT_TRUE(sec.setAncillary(apex::compat::bytes_span{anc.data(), anc.size()}));
  const std::size_t SEC_LEN = sec.length();

  // Act
  auto msgOpt = MutableSppMessageFactory::build<MyPayload2>(
      /*includeSecondary*/ true,
      /*version*/ 0,
      /*type*/ true, // TC
      /*apid*/ 0x0456,
      /*seqFlags*/ 2,
      /*seqCount*/ 100,
      /*payloadInstance*/ payload,
      /*secondaryHeader*/ std::optional<MutableSppSecondaryHeaderDefault>{sec});

  // Assert
  ASSERT_TRUE(msgOpt.has_value());
  const auto PACKED_OPT = msgOpt->pack();
  ASSERT_TRUE(PACKED_OPT.has_value());
  const auto PACKET = PACKED_OPT->data();

  const std::size_t PAYLOAD_SIZE = sizeof(MyPayload2);
  ASSERT_EQ(PACKET.size(), SPP_HDR_SIZE_BYTES + SEC_LEN + PAYLOAD_SIZE);

  // PD = (SEC_LEN + PAYLOAD_SIZE - 1)
  const std::uint16_t PD = static_cast<std::uint16_t>((PACKET[4] << 8) | PACKET[5]);
  EXPECT_EQ(PD, static_cast<std::uint16_t>((SEC_LEN + PAYLOAD_SIZE) - 1));

  // Secondary region exact match
  std::vector<std::uint8_t> secExpected;
  secExpected.reserve(SEC_LEN);
  secExpected.insert(secExpected.end(), tc.begin(), tc.end());
  secExpected.insert(secExpected.end(), anc.begin(), anc.end());

  std::vector<std::uint8_t> secOut(PACKET.data() + SPP_HDR_SIZE_BYTES,
                                   PACKET.data() + SPP_HDR_SIZE_BYTES + SEC_LEN);
  EXPECT_EQ(secOut, secExpected);
}

/**
 * @test Create using a payload span (apex::compat::rospan).
 * Layout: [Primary(6)] [Payload...]
 */
TEST(MutableSppMessageTest, FactoryWithPayloadSpan) {
  // Arrange
  std::vector<MyPayload1> items;
  items.push_back({0xDEADBEEF});

  // Act
  auto msgOpt = MutableSppMessageFactory::build<MyPayload1>(
      /*includeSecondary*/ false,
      /*version*/ 0,
      /*type*/ false, // TM
      /*apid*/ 0x0789,
      /*seqFlags*/ 0,
      /*seqCount*/ 10,
      /*payloadSpan*/ apex::compat::rospan<MyPayload1>{items.data(), items.size()});

  // Assert
  ASSERT_TRUE(msgOpt.has_value());
  const auto PACKED_OPT = msgOpt->pack();
  ASSERT_TRUE(PACKED_OPT.has_value());
  const auto PACKET = PACKED_OPT->data();

  const std::size_t PAYLOAD_BYTES = items.size() * sizeof(MyPayload1);
  ASSERT_EQ(PACKET.size(), SPP_HDR_SIZE_BYTES + PAYLOAD_BYTES);

  const std::uint16_t PD = static_cast<std::uint16_t>((PACKET[4] << 8) | PACKET[5]);
  EXPECT_EQ(PD, static_cast<std::uint16_t>(PAYLOAD_BYTES - 1));
}

/**
 * @test External mutation of payload and header fields is reflected on pack().
 */
TEST(MutableSppMessageTest, ExternalMutationReflected) {
  // Arrange
  MyPayload1 payload{0x11111111};

  auto msgOpt = MutableSppMessageFactory::build<MyPayload1>(
      /*includeSecondary*/ false,
      /*version*/ 0,
      /*type*/ false, // TM
      /*apid*/ 0x0321,
      /*seqFlags*/ 1,
      /*seqCount*/ 55,
      /*payloadInstance*/ payload);
  ASSERT_TRUE(msgOpt.has_value());

  auto beforeOpt = msgOpt->pack();
  ASSERT_TRUE(beforeOpt.has_value());
  const auto BEFORE = beforeOpt->data();
  std::vector<std::uint8_t> beforeVec(BEFORE.data(), BEFORE.data() + BEFORE.size());

  // Mutate external payload and header field
  payload.sensorData = 0x22222222;
  msgOpt->priHdr.seqCount = 56;

  auto afterOpt = msgOpt->pack();
  ASSERT_TRUE(afterOpt.has_value());
  const auto AFTER = afterOpt->data();
  std::vector<std::uint8_t> afterVec(AFTER.data(), AFTER.data() + AFTER.size());

  // Length unchanged
  ASSERT_EQ(beforeVec.size(), afterVec.size());

  // Payload bytes reflect new sensorData
  const std::size_t PAYLOAD_SIZE = sizeof(MyPayload1);
  std::vector<std::uint8_t> afterPayload(afterVec.begin() + SPP_HDR_SIZE_BYTES, afterVec.end());

  std::vector<std::uint8_t> expected(PAYLOAD_SIZE);
  std::memcpy(expected.data(), &payload, PAYLOAD_SIZE);
  EXPECT_EQ(afterPayload, expected);

  // Header bytes 2-3 carry seqFlags/seqCount fragments; expect a change
  const bool HEADER_CHANGED = (beforeVec[2] != afterVec[2]) || (beforeVec[3] != afterVec[3]);
  EXPECT_TRUE(HEADER_CHANGED);
}

/**
 * @test Zero-alloc writer: packInto() matches pack() exactly.
 */
TEST(MutableSppMessageTest, PackIntoMatchesPack) {
  // Arrange
  struct P {
    std::uint16_t a;
    std::uint16_t b;
  };
  P payload{0x0A0B, 0x0C0D};

  MutableSppSecondaryHeaderDefault sec;
  std::array<std::uint8_t, 3> tc{0x01, 0x02, 0x03};
  std::array<std::uint8_t, 1> anc{0x04};
  ASSERT_TRUE(sec.setTimeCode(apex::compat::bytes_span{tc.data(), tc.size()}));
  ASSERT_TRUE(sec.setAncillary(apex::compat::bytes_span{anc.data(), anc.size()}));

  auto msgOpt = MutableSppMessageFactory::build<P>(
      /*includeSecondary*/ true,
      /*version*/ 1,
      /*type*/ true, // TC
      /*apid*/ 0x015,
      /*seqFlags*/ 2,
      /*seqCount*/ 7,
      /*payloadInstance*/ payload,
      /*secondaryHeader*/ std::optional<MutableSppSecondaryHeaderDefault>{sec});
  ASSERT_TRUE(msgOpt.has_value());

  // Get owned bytes to know expected length
  auto ownedOpt = msgOpt->pack();
  ASSERT_TRUE(ownedOpt.has_value());
  const auto OWNED = ownedOpt->data();
  std::vector<std::uint8_t> ownedVec(OWNED.data(), OWNED.data() + OWNED.size());

  // Act: packInto()
  std::vector<std::uint8_t> out(ownedVec.size(), 0x00);
  auto written = msgOpt->packInto(out.data(), out.size());

  // Assert
  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(*written, ownedVec.size());
  EXPECT_EQ(out, ownedVec);
}

/**
 * @test Span with multiple payload elements (apex::compat::rospan).
 * Verifies PD and payload byte region for N>1 elements.
 */
TEST(MutableSppMessageTest, SpanMultipleElements) {
  // Arrange
  struct P {
    std::uint32_t v;
  };
  std::vector<P> items{{0xAA55AA55}, {0x12345678}, {0xFFFFFFFF}};

  auto msgOpt = MutableSppMessageFactory::build<P>(
      /*includeSecondary*/ false,
      /*version*/ 0,
      /*type*/ false, // TM
      /*apid*/ 0x002,
      /*seqFlags*/ 0,
      /*seqCount*/ 1,
      /*payloadSpan*/ apex::compat::rospan<P>{items.data(), items.size()});
  ASSERT_TRUE(msgOpt.has_value());

  // Act
  auto packedOpt = msgOpt->pack();
  ASSERT_TRUE(packedOpt.has_value());
  const auto PACKET = packedOpt->data();

  // Assert
  const std::size_t PAYLOAD_BYTES = items.size() * sizeof(P);
  ASSERT_EQ(PACKET.size(), SPP_HDR_SIZE_BYTES + PAYLOAD_BYTES);
  const std::uint16_t PD = static_cast<std::uint16_t>((PACKET[4] << 8) | PACKET[5]);
  EXPECT_EQ(PD, static_cast<std::uint16_t>(PAYLOAD_BYTES - 1));

  // Payload region matches source memory
  std::vector<std::uint8_t> expected(PAYLOAD_BYTES);
  std::memcpy(expected.data(), items.data(), PAYLOAD_BYTES);
  std::vector<std::uint8_t> actual(PACKET.data() + SPP_HDR_SIZE_BYTES,
                                   PACKET.data() + PACKET.size());
  EXPECT_EQ(actual, expected);
}

/**
 * @test Empty span is rejected by the factory.
 */
TEST(MutableSppMessageTest, EmptySpanRejected) {
  struct P {
    std::uint8_t x;
  };
  // Default-constructed rospan is empty on both C++17 shim and C++20 alias.
  apex::compat::rospan<P> empty{};

  auto msgOpt = MutableSppMessageFactory::build<P>(
      /*includeSecondary*/ false,
      /*version*/ 0,
      /*type*/ false, // TM
      /*apid*/ 0x001,
      /*seqFlags*/ 0,
      /*seqCount*/ 0,
      /*payloadSpan*/ empty);

  EXPECT_FALSE(msgOpt.has_value());
}

/**
 * @test Out-of-range primary-header fields propagate failure when packing.
 * (Factory builds the object; pack() should fail via SppMsg validation.)
 */
TEST(MutableSppMessageTest, InvalidFieldValuesRejectedByPack) {
  struct P {
    std::uint8_t x;
  } payload{0x01};

  // version > 7
  auto vBad = MutableSppMessageFactory::build<P>(false, /*version*/ 8, false, 0x001, 0, 0, payload);
  ASSERT_TRUE(vBad.has_value());
  EXPECT_FALSE(vBad->pack().has_value());

  // apid > 0x7FF
  auto apidBad = MutableSppMessageFactory::build<P>(false, 0, false, 0x1800, 0, 0, payload);
  ASSERT_TRUE(apidBad.has_value());
  EXPECT_FALSE(apidBad->pack().has_value());

  // seqFlags > 3
  auto flagsBad = MutableSppMessageFactory::build<P>(false, 0, false, 0x001, 4, 0, payload);
  ASSERT_TRUE(flagsBad.has_value());
  EXPECT_FALSE(flagsBad->pack().has_value());

  // seqCount > 0x3FFF
  auto countBad = MutableSppMessageFactory::build<P>(false, 0, false, 0x001, 0, 0x4000, payload);
  ASSERT_TRUE(countBad.has_value());
  EXPECT_FALSE(countBad->pack().has_value());
}

/**
 * @test includeSecondary=true but both timeCode and ancillary empty -> final packet has NO
 * secondary header. Validate via primary-header flag using the viewer's primary view.
 */
TEST(MutableSppMessageTest, IncludeSecondaryTrueButEmptyDataNoSecondaryInPacket) {

  // Arrange
  struct P {
    std::uint8_t x;
  } payload{0xEE};
  MutableSppSecondaryHeaderDefault sec; // both fields empty

  auto msgOpt = MutableSppMessageFactory::build<P>(
      /*includeSecondary*/ true, // user requested, but empty data
      /*version*/ 0,
      /*type*/ false, // TM
      /*apid*/ 0x010,
      /*seqFlags*/ 0,
      /*seqCount*/ 0,
      /*payloadInstance*/ payload,
      /*secondaryHeader*/ std::optional<MutableSppSecondaryHeaderDefault>{sec});
  ASSERT_TRUE(msgOpt.has_value());

  // Act
  auto packedOpt = msgOpt->pack();
  ASSERT_TRUE(packedOpt.has_value());
  const auto PACKET = packedOpt->data();

  // Assert: primary header says no secondary; PD == payload-1; total = 6 + payload
  ASSERT_EQ(PACKET.size(), SPP_HDR_SIZE_BYTES + sizeof(P));
  PrimaryHeaderView ph{apex::compat::bytes_span{PACKET.data(), SPP_HDR_SIZE_BYTES}};
  EXPECT_FALSE(ph.hasSecondaryHeader());
  EXPECT_EQ(ph.packetDataLength(), static_cast<std::uint16_t>(sizeof(P) - 1));
}

/**
 * @test Secondary-only packet (timeCode only, no user data) is accepted.
 * PD == secLen - 1; primary header flag indicates secondary present.
 */
TEST(MutableSppMessageTest, SecondaryOnly_TimeCode_NoUserData) {

  // Arrange: no user payload; secondary header has only timeCode
  struct P {
    std::uint8_t dummy;
  };
  MutableSppSecondaryHeaderDefault sec;
  std::array<std::uint8_t, 4> tc{0x99, 0x88, 0x77, 0x66};
  ASSERT_TRUE(sec.setTimeCode(apex::compat::bytes_span{tc.data(), tc.size()}));
  const std::size_t SEC_LEN = sec.length(); // 4

  // Build with empty span (allowed because secondary contributes bytes)
  apex::compat::rospan<P> emptySpan{};
  auto msgOpt = MutableSppMessageFactory::build<P>(
      /*includeSecondary*/ true,
      /*version*/ 0,
      /*type*/ false, // TM
      /*apid*/ 0x020,
      /*seqFlags*/ 0,
      /*seqCount*/ 5,
      /*payloadSpan*/ emptySpan,
      /*secondaryHeader*/ std::optional<MutableSppSecondaryHeaderDefault>{sec});
  ASSERT_TRUE(msgOpt.has_value());

  // Act
  auto packedOpt = msgOpt->pack();
  ASSERT_TRUE(packedOpt.has_value());
  const auto PACKET = packedOpt->data();

  // Assert: total = 6 + SEC_LEN; PD = SEC_LEN - 1
  ASSERT_EQ(PACKET.size(), SPP_HDR_SIZE_BYTES + SEC_LEN);
  PrimaryHeaderView ph{apex::compat::bytes_span{PACKET.data(), SPP_HDR_SIZE_BYTES}};
  EXPECT_TRUE(ph.hasSecondaryHeader());
  EXPECT_EQ(ph.packetDataLength(), static_cast<std::uint16_t>(SEC_LEN - 1));

  // Secondary region matches
  std::vector<std::uint8_t> secOut(PACKET.data() + SPP_HDR_SIZE_BYTES,
                                   PACKET.data() + PACKET.size());
  std::vector<std::uint8_t> tcVec(tc.begin(), tc.end());
  EXPECT_EQ(secOut, tcVec);
}

/**
 * @test Secondary-only packet (ancillary only) via packInto().
 */
TEST(MutableSppMessageTest, SecondaryOnly_Ancillary_NoUserData_PackInto) {

  // Arrange
  struct P {
    std::uint8_t dummy;
  };
  MutableSppSecondaryHeaderDefault sec;
  std::array<std::uint8_t, 3> anc{0xAA, 0xBB, 0xCC};
  ASSERT_TRUE(sec.setAncillary(apex::compat::bytes_span{anc.data(), anc.size()}));
  const std::size_t SEC_LEN = sec.length(); // 3

  // Build with pointer+count payload = 0 (allowed since secondary contributes)
  auto msgOpt = MutableSppMessageFactory::build<P>(
      /*includeSecondary*/ true,
      /*version*/ 0,
      /*type*/ true, // TC
      /*apid*/ 0x021,
      /*seqFlags*/ 1,
      /*seqCount*/ 9,
      /*payloadPtr*/ static_cast<const P*>(nullptr),
      /*payloadCount*/ 0,
      /*secondaryHeader*/ std::optional<MutableSppSecondaryHeaderDefault>{sec});
  ASSERT_TRUE(msgOpt.has_value());

  // Act: packInto into exact-sized buffer
  const std::size_t TOTAL = SPP_HDR_SIZE_BYTES + SEC_LEN;
  std::vector<std::uint8_t> out(TOTAL, 0x00);
  auto written = msgOpt->packInto(out.data(), out.size());

  // Assert
  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(*written, TOTAL);

  PrimaryHeaderView ph{apex::compat::bytes_span{out.data(), SPP_HDR_SIZE_BYTES}};
  EXPECT_TRUE(ph.hasSecondaryHeader());
  EXPECT_EQ(ph.packetDataLength(), static_cast<std::uint16_t>(SEC_LEN - 1));

  // Check secondary region
  std::vector<std::uint8_t> secOut(out.begin() + SPP_HDR_SIZE_BYTES, out.end());
  std::vector<std::uint8_t> ancVec(anc.begin(), anc.end());
  EXPECT_EQ(secOut, ancVec);
}

/**
 * @test Maximal packet length is accepted. Choose sizes so total == MAX_SPP_PACKET_LENGTH.
 * No secondary header for simplicity.
 */
TEST(MutableSppMessageTest, MaxPacketLengthAccepted_Mutable) {
  struct Byte {
    std::uint8_t b;
  };
  const std::size_t MAX_TOTAL = protocols::ccsds::spp::MAX_SPP_PACKET_LENGTH;
  const std::size_t PAYLOAD_LEN =
      (MAX_TOTAL > SPP_HDR_SIZE_BYTES) ? (MAX_TOTAL - SPP_HDR_SIZE_BYTES) : 0u;
  ASSERT_GT(PAYLOAD_LEN, 0u);

  std::vector<Byte> items(PAYLOAD_LEN); // 1B each -> payloadBytes == PAYLOAD_LEN

  auto msgOpt = MutableSppMessageFactory::build<Byte>(
      /*includeSecondary*/ false,
      /*version*/ 0,
      /*type*/ false, // TM
      /*apid*/ 0x003,
      /*seqFlags*/ 0,
      /*seqCount*/ 0,
      /*payloadSpan*/ apex::compat::rospan<Byte>{items.data(), items.size()});
  ASSERT_TRUE(msgOpt.has_value());

  auto packedOpt = msgOpt->pack();
  ASSERT_TRUE(packedOpt.has_value());
  const auto PACKET = packedOpt->data();

  EXPECT_EQ(PACKET.size(), MAX_TOTAL);
  const std::uint16_t PD = static_cast<std::uint16_t>((PACKET[4] << 8) | PACKET[5]);
  EXPECT_EQ(static_cast<std::size_t>(PD) + 1u, PAYLOAD_LEN); // PD+1 == data field bytes
}

/**
 * @test Packet length exceeding MAX_SPP_PACKET_LENGTH is rejected.
 */
TEST(MutableSppMessageTest, PacketLengthExceedsMax_Mutable) {
  struct Byte {
    std::uint8_t b;
  };
  const std::size_t TOO_BIG_PAYLOAD =
      (protocols::ccsds::spp::MAX_SPP_PACKET_LENGTH > SPP_HDR_SIZE_BYTES)
          ? (protocols::ccsds::spp::MAX_SPP_PACKET_LENGTH - SPP_HDR_SIZE_BYTES + 1)
          : 1u;

  std::vector<Byte> items(TOO_BIG_PAYLOAD);

  auto msgOpt = MutableSppMessageFactory::build<Byte>(
      /*includeSecondary*/ false,
      /*version*/ 0,
      /*type*/ false, // TM
      /*apid*/ 0x004,
      /*seqFlags*/ 0,
      /*seqCount*/ 0,
      /*payloadSpan*/ apex::compat::rospan<Byte>{items.data(), items.size()});
  ASSERT_TRUE(msgOpt.has_value());

  // pack() should fail due to size limit
  EXPECT_FALSE(msgOpt->pack().has_value());

  // packInto() with too-small buffer should also fail
  std::vector<std::uint8_t> out(16, 0x00);
  EXPECT_FALSE(msgOpt->packInto(out.data(), out.size()).has_value());
}

/**
 * @test packInto() returns std::nullopt when the caller buffer is too small (but packet is
 * otherwise valid).
 */
TEST(MutableSppMessageTest, PackIntoTooSmallBuffer) {
  struct P {
    std::uint32_t x;
  } p{0xCAFEBABE};

  auto msgOpt = MutableSppMessageFactory::build<P>(
      /*includeSecondary*/ false,
      /*version*/ 0,
      /*type*/ false, // TM
      /*apid*/ 0x005,
      /*seqFlags*/ 0,
      /*seqCount*/ 0,
      /*payloadInstance*/ p);
  ASSERT_TRUE(msgOpt.has_value());

  // Determine required size from pack()
  auto packedOpt = msgOpt->pack();
  ASSERT_TRUE(packedOpt.has_value());
  const auto PACKET = packedOpt->data();
  ASSERT_GT(PACKET.size(), 0u);

  // Provide a buffer that is one byte too small
  std::vector<std::uint8_t> small(PACKET.size() - 1u, 0x00);
  auto w = msgOpt->packInto(small.data(), small.size());
  EXPECT_FALSE(w.has_value());
}

/**
 * @test MutableSppSecondaryHeader setTimeCode/setAncillary validation.
 */
TEST(MutableSppSecondaryHeaderTest, SettersEnforceCapacity) {
  using SmallSecHdr = MutableSppSecondaryHeader<4, 8>;
  SmallSecHdr sec;

  // Should succeed within capacity
  std::array<std::uint8_t, 4> tc{0x11, 0x22, 0x33, 0x44};
  EXPECT_TRUE(sec.setTimeCode(apex::compat::bytes_span{tc.data(), tc.size()}));
  EXPECT_EQ(sec.timeCodeLen, tc.size());

  std::array<std::uint8_t, 8> anc{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  EXPECT_TRUE(sec.setAncillary(apex::compat::bytes_span{anc.data(), anc.size()}));
  EXPECT_EQ(sec.ancillaryLen, anc.size());

  // Should fail when exceeding capacity
  std::array<std::uint8_t, 5> tcTooBig{0x11, 0x22, 0x33, 0x44, 0x55};
  EXPECT_FALSE(sec.setTimeCode(apex::compat::bytes_span{tcTooBig.data(), tcTooBig.size()}));
  EXPECT_EQ(sec.timeCodeLen, tc.size()); // Unchanged

  std::array<std::uint8_t, 9> ancTooBig{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
  EXPECT_FALSE(sec.setAncillary(apex::compat::bytes_span{ancTooBig.data(), ancTooBig.size()}));
  EXPECT_EQ(sec.ancillaryLen, anc.size()); // Unchanged
}

/**
 * @test requiredSize() computes correct size.
 */
TEST(MutableSppMessageTest, RequiredSize) {
  struct P {
    std::uint16_t x;
  } payload{0x1234};

  MutableSppSecondaryHeaderDefault sec;
  std::array<std::uint8_t, 4> tc{0x11, 0x22, 0x33, 0x44};
  ASSERT_TRUE(sec.setTimeCode(apex::compat::bytes_span{tc.data(), tc.size()}));

  auto msgOpt = MutableSppMessageFactory::build<P>(
      /*includeSecondary*/ true,
      /*version*/ 0,
      /*type*/ false,
      /*apid*/ 0x010,
      /*seqFlags*/ 0,
      /*seqCount*/ 0,
      /*payloadInstance*/ payload,
      /*secondaryHeader*/ std::optional<MutableSppSecondaryHeaderDefault>{sec});
  ASSERT_TRUE(msgOpt.has_value());

  const std::size_t EXPECTED = SPP_HDR_SIZE_BYTES + tc.size() + sizeof(P);
  EXPECT_EQ(msgOpt->requiredSize(), EXPECTED);
}
