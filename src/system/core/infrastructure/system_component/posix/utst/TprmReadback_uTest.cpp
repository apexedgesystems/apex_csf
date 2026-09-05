/**
 * @file TprmReadback_uTest.cpp
 * @brief Unit tests for the staged-bank digest (READBACK_TPRM assembly).
 *
 * Coverage:
 *   - Valid v3 payloads report declared identity with verdict OK
 *   - Corrupt preludes (bad magic, truncation, size lie) surface as
 *     rows with the matching verdict, never as silent skips
 *   - Filename ordering and pagination (offset past end, partial page)
 *   - Empty and unreadable staged directories
 */

#include "src/system/core/infrastructure/system_component/posix/inc/TprmReadback.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#include <gtest/gtest.h>

using system_core::system_component::appendStagedDigest;
using system_core::system_component::READBACK_MAX_ROWS;
using system_core::system_component::READBACK_PAGE_HEADER_SIZE;
using system_core::system_component::READBACK_ROW_SIZE;
using system_core::system_component::TprmPayloadCheck;
using system_core::system_component::TprmPayloadHeader;

namespace {

struct Row {
  std::uint32_t fullUid;
  std::uint32_t layoutHash;
  std::uint32_t payloadCrc;
  std::uint8_t verdict;
};

struct Page {
  std::uint16_t total;
  std::uint16_t offset;
  std::uint8_t count;
  std::vector<Row> rows;
};

std::uint16_t rd16(const std::vector<std::uint8_t>& b, std::size_t o) {
  return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}
std::uint32_t rd32(const std::vector<std::uint8_t>& b, std::size_t o) {
  std::uint32_t v = 0;
  std::memcpy(&v, b.data() + o, 4);
  return v;
}

Page parsePage(const std::vector<std::uint8_t>& b) {
  Page p{};
  EXPECT_GE(b.size(), READBACK_PAGE_HEADER_SIZE);
  p.total = rd16(b, 0);
  p.offset = rd16(b, 2);
  p.count = b[4];
  EXPECT_EQ(b.size(), READBACK_PAGE_HEADER_SIZE + p.count * READBACK_ROW_SIZE);
  for (std::size_t i = 0; i < p.count; ++i) {
    const std::size_t O = READBACK_PAGE_HEADER_SIZE + i * READBACK_ROW_SIZE;
    p.rows.push_back({rd32(b, O), rd32(b, O + 4), rd32(b, O + 8), b[O + 12]});
  }
  return p;
}

class TprmReadbackTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::random_device rd;
    dir_ = std::filesystem::temp_directory_path() / ("tprm_readback_" + std::to_string(rd()));
    std::filesystem::create_directories(dir_);
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  void writeStaged(const std::string& name, std::uint32_t uid, std::uint32_t layoutHash,
                   std::uint32_t crc, std::size_t bodyLen, bool corruptMagic = false,
                   bool lieAboutSize = false) {
    TprmPayloadHeader h{};
    std::memcpy(h.magic.data(), corruptMagic ? "NOPE" : "APV3", 4);
    h.version = 3;
    h.payloadSize = static_cast<std::uint16_t>(lieAboutSize ? bodyLen + 7 : bodyLen);
    h.fullUid = uid;
    h.layoutHash = layoutHash;
    h.payloadCrc = crc;
    std::ofstream f(dir_ / name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(&h), sizeof(h));
    const std::vector<char> body(bodyLen, 0x5A);
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
  }

  std::filesystem::path dir_;
};

} // namespace

/** @test Valid staged payloads report declared identity, verdict OK. */
TEST_F(TprmReadbackTest, ValidPayloadsReportDeclaredIdentity) {
  writeStaged("000500.tprm", 0x000500U, 0xAABBCCDDU, 0x11223344U, 64);
  writeStaged("00c800.tprm", 0x00C800U, 0x55667788U, 0x99AABBCCU, 122);

  std::vector<std::uint8_t> out;
  ASSERT_TRUE(appendStagedDigest(dir_, 0, out));
  const Page P = parsePage(out);
  ASSERT_EQ(P.total, 2U);
  ASSERT_EQ(P.count, 2U);
  EXPECT_EQ(P.rows[0].fullUid, 0x000500U);
  EXPECT_EQ(P.rows[0].layoutHash, 0xAABBCCDDU);
  EXPECT_EQ(P.rows[0].payloadCrc, 0x11223344U);
  EXPECT_EQ(P.rows[0].verdict, static_cast<std::uint8_t>(TprmPayloadCheck::OK));
  EXPECT_EQ(P.rows[1].fullUid, 0x00C800U);
  EXPECT_EQ(P.rows[1].verdict, static_cast<std::uint8_t>(TprmPayloadCheck::OK));
}

/** @test Corrupt preludes surface as rows with the matching verdict. */
TEST_F(TprmReadbackTest, CorruptPreludesAreFindingsNotSkips) {
  writeStaged("000500.tprm", 0x000500U, 1, 2, 16, /*corruptMagic=*/true);
  writeStaged("000600.tprm", 0x000600U, 3, 4, 16, false, /*lieAboutSize=*/true);
  {
    std::ofstream f(dir_ / "000700.tprm", std::ios::binary);
    f.write("APV", 3); // Truncated below the prelude.
  }

  std::vector<std::uint8_t> out;
  ASSERT_TRUE(appendStagedDigest(dir_, 0, out));
  const Page P = parsePage(out);
  ASSERT_EQ(P.total, 3U);
  EXPECT_EQ(P.rows[0].verdict, static_cast<std::uint8_t>(TprmPayloadCheck::BAD_MAGIC));
  EXPECT_EQ(P.rows[1].verdict, static_cast<std::uint8_t>(TprmPayloadCheck::SIZE_MISMATCH));
  EXPECT_EQ(P.rows[1].fullUid, 0x000600U) << "declared fields reported even on failure";
  EXPECT_EQ(P.rows[2].verdict, static_cast<std::uint8_t>(TprmPayloadCheck::TOO_SMALL));
}

/** @test Pagination is filename-ordered and stable; offsets clamp. */
TEST_F(TprmReadbackTest, PaginationIsStableAndClamped) {
  for (int i = 0; i < 70; ++i) {
    char name[16];
    std::snprintf(name, sizeof(name), "%06x.tprm", 0x1000 + i);
    writeStaged(name, static_cast<std::uint32_t>(0x1000 + i), 0, 0, 8);
  }

  std::vector<std::uint8_t> page1;
  ASSERT_TRUE(appendStagedDigest(dir_, 0, page1));
  const Page P1 = parsePage(page1);
  EXPECT_EQ(P1.total, 70U);
  EXPECT_EQ(P1.count, READBACK_MAX_ROWS);
  EXPECT_EQ(P1.rows.front().fullUid, 0x1000U);

  std::vector<std::uint8_t> page2;
  ASSERT_TRUE(appendStagedDigest(dir_, READBACK_MAX_ROWS, page2));
  const Page P2 = parsePage(page2);
  EXPECT_EQ(P2.count, 70U - READBACK_MAX_ROWS);
  EXPECT_EQ(P2.rows.front().fullUid, 0x1000U + READBACK_MAX_ROWS);

  std::vector<std::uint8_t> beyond;
  ASSERT_TRUE(appendStagedDigest(dir_, 999, beyond));
  const Page P3 = parsePage(beyond);
  EXPECT_EQ(P3.count, 0U);
  EXPECT_EQ(P3.total, 70U);
}

/** @test Empty staged bank is a valid zero-row page; unreadable dir fails. */
TEST_F(TprmReadbackTest, EmptyBankAndUnreadableDir) {
  std::vector<std::uint8_t> out;
  ASSERT_TRUE(appendStagedDigest(dir_, 0, out));
  const Page P = parsePage(out);
  EXPECT_EQ(P.total, 0U);
  EXPECT_EQ(P.count, 0U);

  std::vector<std::uint8_t> bad;
  EXPECT_FALSE(appendStagedDigest(dir_ / "no_such_subdir", 0, bad));
}

/* ----------------------------- Verify Verdict ----------------------------- */

namespace {

using system_core::system_component::appendVerifyVerdict;
using system_core::system_component::tprmCrc32;
using system_core::system_component::VERIFY_RESPONSE_SIZE;

/// Write a staged payload whose CRC (and optionally layout hash) are genuine.
void writeVerified(const std::filesystem::path& dir, std::uint32_t uid, std::uint32_t layoutHash,
                   const std::vector<std::uint8_t>& body, bool corruptCrc = false) {
  TprmPayloadHeader h{};
  std::memcpy(h.magic.data(), "APV3", 4);
  h.version = 3;
  h.payloadSize = static_cast<std::uint16_t>(body.size());
  h.fullUid = uid;
  h.layoutHash = layoutHash;
  h.payloadCrc = tprmCrc32(body.data(), body.size()) ^ (corruptCrc ? 0xFFU : 0U);
  std::ofstream f(dir / system_core::system_component::SystemComponentBase::tprmFilename(uid),
                  std::ios::binary);
  f.write(reinterpret_cast<const char*>(&h), sizeof(h));
  f.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
}

} // namespace

/** @test A genuine staged payload verifies OK; the verdict block carries its hashes. */
TEST_F(TprmReadbackTest, VerifyAcceptsGenuinePayload) {
  const std::vector<std::uint8_t> BODY(64, 0x3C);
  writeVerified(dir_, 0x00C800U, 0xFEEDF00DU, BODY);

  std::vector<std::uint8_t> out;
  const auto VERDICT = appendVerifyVerdict(dir_, 0x00C800U, nullptr, out);
  EXPECT_EQ(VERDICT, TprmPayloadCheck::OK);
  ASSERT_EQ(out.size(), VERIFY_RESPONSE_SIZE);
  EXPECT_EQ(out[0], static_cast<std::uint8_t>(TprmPayloadCheck::OK));
  EXPECT_EQ(rd32(out, 4), 0xFEEDF00DU);
}

/** @test Body corruption and wrong-target staging produce distinct verdicts. */
TEST_F(TprmReadbackTest, VerifyDistinguishesFailureClasses) {
  const std::vector<std::uint8_t> BODY(32, 0x11);
  writeVerified(dir_, 0x000500U, 0, BODY, /*corruptCrc=*/true);

  std::vector<std::uint8_t> out;
  EXPECT_EQ(appendVerifyVerdict(dir_, 0x000500U, nullptr, out), TprmPayloadCheck::CRC_MISMATCH);

  // Wrong target: payload declares a different uid than its filename slot.
  writeVerified(dir_, 0x000600U, 0, BODY);
  std::filesystem::rename(
      dir_ / system_core::system_component::SystemComponentBase::tprmFilename(0x000600U),
      dir_ / system_core::system_component::SystemComponentBase::tprmFilename(0x000700U));
  out.clear();
  EXPECT_EQ(appendVerifyVerdict(dir_, 0x000700U, nullptr, out), TprmPayloadCheck::UID_MISMATCH);

  // Missing staged file.
  out.clear();
  EXPECT_EQ(appendVerifyVerdict(dir_, 0x00BB00U, nullptr, out), TprmPayloadCheck::FILE_ERROR);
}

/** @test Layout-hash enforcement fires only when an expected hash is supplied. */
TEST_F(TprmReadbackTest, VerifyEnforcesExpectedLayoutHash) {
  const std::vector<std::uint8_t> BODY(16, 0x77);
  writeVerified(dir_, 0x00D700U, 0xAAAA5555U, BODY);

  std::vector<std::uint8_t> out;
  const std::uint32_t GOOD = 0xAAAA5555U;
  EXPECT_EQ(appendVerifyVerdict(dir_, 0x00D700U, &GOOD, out), TprmPayloadCheck::OK);

  const std::uint32_t WRONG = 0x12345678U;
  out.clear();
  EXPECT_EQ(appendVerifyVerdict(dir_, 0x00D700U, &WRONG, out), TprmPayloadCheck::LAYOUT_MISMATCH);
  EXPECT_EQ(rd32(out, 4), 0xAAAA5555U) << "declared hash reported so ground sees the delta";
}
