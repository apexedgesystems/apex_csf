/**
 * @file CcsdsSppTimeCode_uTest.cpp
 * @brief Unit tests for CCSDS time code parsing in SPP packets.
 */

#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppViewer.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppCommonDefs.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsTimeCode.hpp"

#include <gtest/gtest.h>
#include <vector>
#include <cstdint>

using namespace protocols::ccsds::spp;
using namespace protocols::ccsds::common;

class TimeCodeParsingTest : public ::testing::Test {
protected:
  std::vector<std::uint8_t> buildPrimaryHeader(std::uint8_t version, bool type, bool secHdr,
                                               std::uint16_t apid, std::uint8_t seqFlags,
                                               std::uint16_t seqCount, std::size_t dataFieldLen) {

    std::vector<std::uint8_t> hdr(SPP_HDR_SIZE_BYTES);

    const std::uint16_t PD = static_cast<std::uint16_t>(dataFieldLen - 1);

    hdr[0] = (version << SPP_VERSION_SHIFT) | (type ? SPP_TYPE_BIT_MASK : 0) |
             (secHdr ? SPP_SECHDR_BIT_MASK : 0) | ((apid >> 8) & SPP_APID_UPPER_MASK3);
    hdr[1] = apid & 0xFF;
    hdr[2] = (seqFlags << SPP_SEQFLAGS_SHIFT) | ((seqCount >> 8) & SPP_SEQCOUNT_UPPER6_MASK);
    hdr[3] = seqCount & 0xFF;
    hdr[4] = (PD >> 8) & 0xFF;
    hdr[5] = PD & 0xFF;

    return hdr;
  }

  std::vector<std::uint8_t> createPacket(const std::vector<std::uint8_t>& primaryHdr,
                                         const std::vector<std::uint8_t>& secondaryHdr,
                                         const std::vector<std::uint8_t>& payload) {

    std::vector<std::uint8_t> packet;
    packet.insert(packet.end(), primaryHdr.begin(), primaryHdr.end());
    packet.insert(packet.end(), secondaryHdr.begin(), secondaryHdr.end());
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
  }
};

/* ========================= CUC Time Code Tests ============================= */

/** @test CUC Level 1 (4+0): 4-byte coarse time, no fine time. */
TEST_F(TimeCodeParsingTest, CUC_Level1_4_0_CoarseOnly) {
  // Per CCSDS 301.0-B-4 Section 3.2: compact binary elapsed time

  std::vector<std::uint8_t> timeCode = {
      0x12, 0x34, 0x56, 0x78 // 4-byte coarse time = 0x12345678
  };

  std::vector<std::uint8_t> payload = {0xAA, 0xBB, 0xCC};
  auto primary = buildPrimaryHeader(0, false, true, 0x100, 3, 42, timeCode.size() + payload.size());
  auto packet = createPacket(primary, timeCode, payload);

  SecondaryHeaderConfig cfg{};
  cfg.totalLength = timeCode.size();
  cfg.timeCodeFormat = TimeCodeFormat::CUC_LEVEL1_4_0;
  cfg.timeCodeLength = 4;

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;

  ASSERT_TRUE(view.sec.hasTimeCode());
  auto tc = view.sec.timeCode();

  EXPECT_EQ(tc.format, TimeCodeFormat::CUC_LEVEL1_4_0);
  EXPECT_EQ(tc.cuc.coarseOctets, 4);
  EXPECT_EQ(tc.cuc.fineOctets, 0);
  EXPECT_EQ(tc.cuc.coarse, 0x12345678u);
  EXPECT_EQ(tc.cuc.fine, 0u);
  EXPECT_EQ(tc.cuc.epoch, TimeEpoch::CCSDS);
}

/** @test CUC Level 1 (4+1): 4-byte coarse + 1-byte fine time. */
TEST_F(TimeCodeParsingTest, CUC_Level1_4_1_CoarsePlusFine) {

  std::vector<std::uint8_t> timeCode = {
      0xAA, 0xBB, 0xCC, 0xDD, // Coarse
      0x80                    // Fine (0x80 = 0.5 seconds in 1-byte fine)
  };

  std::vector<std::uint8_t> payload = {0x11};
  auto primary =
      buildPrimaryHeader(0, false, true, 0x200, 3, 100, timeCode.size() + payload.size());
  auto packet = createPacket(primary, timeCode, payload);

  SecondaryHeaderConfig cfg{};
  cfg.totalLength = timeCode.size();
  cfg.timeCodeFormat = TimeCodeFormat::CUC_LEVEL1_4_1;
  cfg.timeCodeLength = 5;

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;

  auto tc = view.sec.timeCode();

  EXPECT_EQ(tc.cuc.coarseOctets, 4);
  EXPECT_EQ(tc.cuc.fineOctets, 1);
  EXPECT_EQ(tc.cuc.coarse, 0xAABBCCDDu);
  EXPECT_EQ(tc.cuc.fine, 0x80u);
}

/** @test CUC Level 1 (4+2): 4-byte coarse + 2-byte fine time. */
TEST_F(TimeCodeParsingTest, CUC_Level1_4_2_TwoBytesFine) {

  std::vector<std::uint8_t> timeCode = {
      0x00, 0x00, 0x00, 0x01, // Coarse = 1 second
      0x40, 0x00              // Fine = 0.25 seconds (16384/65536)
  };

  std::vector<std::uint8_t> payload = {0xDD};
  auto primary =
      buildPrimaryHeader(0, false, true, 0x300, 3, 200, timeCode.size() + payload.size());
  auto packet = createPacket(primary, timeCode, payload);

  SecondaryHeaderConfig cfg{};
  cfg.totalLength = timeCode.size();
  cfg.timeCodeFormat = TimeCodeFormat::CUC_LEVEL1_4_2;
  cfg.timeCodeLength = 6;

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;

  auto tc = view.sec.timeCode();

  EXPECT_EQ(tc.cuc.coarseOctets, 4);
  EXPECT_EQ(tc.cuc.fineOctets, 2);
  EXPECT_EQ(tc.cuc.coarse, 1u);
  EXPECT_EQ(tc.cuc.fine, 0x4000u);
}

/* ========================= CDS Time Code Tests ============================= */

/** @test CDS Short: 2-byte day count + 4-byte milliseconds. */
TEST_F(TimeCodeParsingTest, CDS_Short_DaysAndMilliseconds) {
  // Per CCSDS 301.0-B-4 Section 3.3: Day segmented time

  std::vector<std::uint8_t> timeCode = {
      0x00, 0x64,            // Days = 100
      0x00, 0x01, 0x51, 0x80 // Milliseconds = 86400 (1 day in ms)
  };

  std::vector<std::uint8_t> payload = {0xFF};
  auto primary =
      buildPrimaryHeader(0, false, true, 0x400, 3, 300, timeCode.size() + payload.size());
  auto packet = createPacket(primary, timeCode, payload);

  SecondaryHeaderConfig cfg{};
  cfg.totalLength = timeCode.size();
  cfg.timeCodeFormat = TimeCodeFormat::CDS_SHORT;
  cfg.timeCodeLength = 6; // CDS Short: 2 bytes days + 4 bytes ms = 6 bytes

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;

  auto tc = view.sec.timeCode();

  EXPECT_EQ(tc.format, TimeCodeFormat::CDS_SHORT);
  EXPECT_FALSE(tc.cds.has24BitDays);
  EXPECT_EQ(tc.cds.days, 100u);
  EXPECT_EQ(tc.cds.milliseconds, 86400u);
  EXPECT_EQ(tc.cds.submilliseconds, 0u);
  EXPECT_EQ(tc.cds.epoch, TimeEpoch::CCSDS);
}

/** @test CDS Long: 3-byte days + 4-byte ms + 2-byte submilliseconds. */
TEST_F(TimeCodeParsingTest, CDS_Long_WithSubmilliseconds) {

  std::vector<std::uint8_t> timeCode = {
      0x00, 0x01, 0x00,       // Days = 256
      0x00, 0x00, 0x03, 0xE8, // Milliseconds = 1000 (1 second)
      0x80, 0x00              // Submilliseconds = 32768 (0.5 ms)
  };

  std::vector<std::uint8_t> payload = {0x22, 0x33};
  auto primary =
      buildPrimaryHeader(0, false, true, 0x500, 3, 400, timeCode.size() + payload.size());
  auto packet = createPacket(primary, timeCode, payload);

  SecondaryHeaderConfig cfg{};
  cfg.totalLength = timeCode.size();
  cfg.timeCodeFormat = TimeCodeFormat::CDS_LONG;
  cfg.timeCodeLength = 9; // CDS Long: 3 bytes days + 4 bytes ms + 2 bytes subms = 9 bytes

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;

  auto tc = view.sec.timeCode();

  EXPECT_EQ(tc.format, TimeCodeFormat::CDS_LONG);
  EXPECT_TRUE(tc.cds.has24BitDays);
  EXPECT_EQ(tc.cds.days, 256u);
  EXPECT_EQ(tc.cds.milliseconds, 1000u);
  EXPECT_EQ(tc.cds.submilliseconds, 32768u);
}

/* ===================== Time Code + Ancillary Data Tests =================== */

/** @test Secondary header with both time code and ancillary data. */
TEST_F(TimeCodeParsingTest, TimeCodePlusAncillaryData) {

  std::vector<std::uint8_t> secondaryHeader = {
      0x12, 0x34, 0x56, 0x78, // Time code (CUC 4+0)
      0xAA, 0xBB, 0xCC, 0xDD  // Ancillary data
  };

  std::vector<std::uint8_t> payload = {0x55, 0x66};
  auto primary =
      buildPrimaryHeader(0, false, true, 0x600, 3, 500, secondaryHeader.size() + payload.size());
  auto packet = createPacket(primary, secondaryHeader, payload);

  SecondaryHeaderConfig cfg{};
  cfg.totalLength = 8;
  cfg.timeCodeFormat = TimeCodeFormat::CUC_LEVEL1_4_0;
  cfg.timeCodeLength = 4;

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;

  EXPECT_TRUE(view.sec.hasTimeCode());
  EXPECT_TRUE(view.sec.hasAncillaryData());

  EXPECT_EQ(view.sec.timeCodeBytes.size(), 4u);
  EXPECT_EQ(view.sec.ancillaryData.size(), 4u);

  auto tc = view.sec.timeCode();
  EXPECT_EQ(tc.cuc.coarse, 0x12345678u);

  EXPECT_EQ(view.sec.ancillaryData[0], 0xAA);
  EXPECT_EQ(view.sec.ancillaryData[1], 0xBB);
  EXPECT_EQ(view.sec.ancillaryData[2], 0xCC);
  EXPECT_EQ(view.sec.ancillaryData[3], 0xDD);
}

/** @test Secondary header with ancillary data only, no time code. */
TEST_F(TimeCodeParsingTest, AncillaryDataOnly) {

  std::vector<std::uint8_t> ancillary = {0x11, 0x22, 0x33, 0x44, 0x55};
  std::vector<std::uint8_t> payload = {0xAA};

  auto primary =
      buildPrimaryHeader(0, false, true, 0x700, 3, 600, ancillary.size() + payload.size());
  auto packet = createPacket(primary, ancillary, payload);

  SecondaryHeaderConfig cfg{};
  cfg.totalLength = 5;
  cfg.timeCodeFormat = TimeCodeFormat::NONE;
  cfg.timeCodeLength = 0;

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;

  EXPECT_FALSE(view.sec.hasTimeCode());
  EXPECT_TRUE(view.sec.hasAncillaryData());
  EXPECT_EQ(view.sec.ancillaryData.size(), 5u);
}

/* ======================= Edge Case Tests ================================== */

/** @test Empty time code (format=NONE, length=0) returns NONE format. */
TEST_F(TimeCodeParsingTest, EmptyTimeCode) {

  std::vector<std::uint8_t> payload = {0x01, 0x02};
  auto primary = buildPrimaryHeader(0, false, false, 0x100, 3, 0, payload.size());
  auto packet = createPacket(primary, {}, payload);

  SecondaryHeaderConfig cfg{};
  cfg.totalLength = 0;
  cfg.timeCodeFormat = TimeCodeFormat::NONE;
  cfg.timeCodeLength = 0;

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);
  ASSERT_TRUE(viewOpt.has_value());
  auto view = *viewOpt;

  EXPECT_FALSE(view.sec.hasTimeCode());

  // Calling timeCode() on empty should return empty result
  auto tc = view.sec.timeCode();
  EXPECT_EQ(tc.format, TimeCodeFormat::NONE);
}

/** @test Maximum 32-bit coarse time value (0xFFFFFFFF). */
TEST_F(TimeCodeParsingTest, MaxCoarseTime) {

  std::vector<std::uint8_t> timeCode = {0xFF, 0xFF, 0xFF, 0xFF};
  std::vector<std::uint8_t> payload = {0x00};

  auto primary = buildPrimaryHeader(0, false, true, 0x100, 3, 1, timeCode.size() + payload.size());
  auto packet = createPacket(primary, timeCode, payload);

  SecondaryHeaderConfig cfg{};
  cfg.totalLength = 4;
  cfg.timeCodeFormat = TimeCodeFormat::CUC_LEVEL1_4_0;
  cfg.timeCodeLength = 4;

  auto viewOpt = PacketViewer::create(apex::compat::bytes_span{packet.data(), packet.size()}, cfg);
  ASSERT_TRUE(viewOpt.has_value());

  auto tc = viewOpt->sec.timeCode();
  EXPECT_EQ(tc.cuc.coarse, 0xFFFFFFFFu);
}