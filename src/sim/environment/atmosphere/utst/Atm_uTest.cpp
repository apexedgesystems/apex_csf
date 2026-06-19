/**
 * @file Atm_uTest.cpp
 * @brief Direct unit tests for the `.atm` file-format reader/writer.
 *
 * The `.atm` reader/writer is the most robustness-critical code in the
 * library: it parses an external, attacker-or-corruption-reachable byte
 * stream. The model tests exercise it only on the happy path (a writer
 * round-trip). These tests hit it directly: header validation for every
 * structural invariant, magic/version rejection, truncated header and
 * body, size-mismatch guards, null-pointer guards, and the record
 * helpers.
 */

#include "src/sim/environment/atmosphere/inc/Atm.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using sim::environment::atmosphere::atmBodySize;
using sim::environment::atmosphere::atmExpectedRecordCount;
using sim::environment::atmosphere::AtmHeader;
using sim::environment::atmosphere::atmHeaderInit;
using sim::environment::atmosphere::atmHeaderValid;
using sim::environment::atmosphere::atmMagicValid;
using sim::environment::atmosphere::atmMakeConstant;
using sim::environment::atmosphere::atmMakeExponential;
using sim::environment::atmosphere::atmMakeLayer;
using sim::environment::atmosphere::AtmModelType;
using sim::environment::atmosphere::AtmReader;
using sim::environment::atmosphere::AtmRecord;
using sim::environment::atmosphere::AtmWriter;
using sim::environment::atmosphere::kAtmHeaderSize;
using sim::environment::atmosphere::kAtmRecordSize;
using sim::environment::atmosphere::kAtmVersion;

namespace {

/* ----------------------------- Fixture ----------------------------- */

class AtmFileTest : public ::testing::Test {
protected:
  void TearDown() override {
    for (const auto& p : created_) {
      std::error_code ec;
      std::filesystem::remove(p, ec);
    }
    created_.clear();
  }

  std::string tmpPath(const char* hint) {
    static int counter = 0;
    ++counter;
    std::string p = "/tmp/atm_uTest_" + std::string(hint) + "_" + std::to_string(::getpid()) + "_" +
                    std::to_string(counter) + ".atm";
    created_.push_back(p);
    return p;
  }

  /// Write `bytes` verbatim to a fresh temp path. Used to forge malformed
  /// files the writer would never emit.
  std::string writeRaw(const char* hint, const void* bytes, std::size_t n) {
    const std::string p = tmpPath(hint);
    std::FILE* f = std::fopen(p.c_str(), "wb");
    if (f == nullptr) {
      return "";
    }
    const std::size_t wrote = (n == 0) ? 0 : std::fwrite(bytes, 1, n, f);
    std::fclose(f);
    return (wrote == n) ? p : "";
  }

private:
  std::vector<std::string> created_;
};

/* ----------------------------- Format constants ----------------------------- */

TEST(AtmFormat, StructSizesMatchWireSpec) {
  EXPECT_EQ(sizeof(AtmHeader), kAtmHeaderSize);
  EXPECT_EQ(sizeof(AtmRecord), kAtmRecordSize);
  EXPECT_EQ(kAtmHeaderSize, 64u);
  EXPECT_EQ(kAtmRecordSize, 32u);
}

/* ----------------------------- Header validation ----------------------------- */

TEST(AtmHeaderValidation, DefaultInitIsValid) {
  AtmHeader h{};
  atmHeaderInit(h);
  EXPECT_TRUE(atmMagicValid(h));
  EXPECT_TRUE(atmHeaderValid(h));
  EXPECT_EQ(h.version, kAtmVersion);
  EXPECT_EQ(h.model_type, static_cast<std::uint8_t>(AtmModelType::kConstant));
  EXPECT_EQ(h.n_records, 1u);
}

TEST(AtmHeaderValidation, BadMagicRejected) {
  AtmHeader h{};
  atmHeaderInit(h);
  h.magic[0] = 'X';
  EXPECT_FALSE(atmMagicValid(h));
  EXPECT_FALSE(atmHeaderValid(h));
}

TEST(AtmHeaderValidation, BadVersionRejected) {
  AtmHeader h{};
  atmHeaderInit(h);
  h.version = kAtmVersion + 1u;
  EXPECT_FALSE(atmHeaderValid(h));
}

TEST(AtmHeaderValidation, OutOfRangeModelTypeRejected) {
  AtmHeader h{};
  atmHeaderInit(h);
  h.model_type = 99u; // beyond kEmpirical (3)
  EXPECT_FALSE(atmHeaderValid(h));
}

TEST(AtmHeaderValidation, NonLayeredRecordCountMustMatchExpected) {
  AtmHeader h{};
  atmHeaderInit(h); // kConstant expects exactly 1
  h.n_records = 2u;
  EXPECT_FALSE(atmHeaderValid(h));
  h.n_records = 1u;
  EXPECT_TRUE(atmHeaderValid(h));
}

TEST(AtmHeaderValidation, LayeredAcceptsAnyPositiveCountButNotZero) {
  AtmHeader h{};
  atmHeaderInit(h);
  h.model_type = static_cast<std::uint8_t>(AtmModelType::kLayered);
  h.n_records = 0u;
  EXPECT_FALSE(atmHeaderValid(h));
  h.n_records = 7u;
  EXPECT_TRUE(atmHeaderValid(h));
}

TEST(AtmHeaderValidation, ThermoInvariants) {
  AtmHeader h{};
  atmHeaderInit(h);
  h.R_specific = 0.0; // must be > 0
  EXPECT_FALSE(atmHeaderValid(h));
  atmHeaderInit(h);
  h.gamma = 1.0; // must be > 1
  EXPECT_FALSE(atmHeaderValid(h));
  atmHeaderInit(h);
  h.g0 = -1.0; // must be > 0
  EXPECT_FALSE(atmHeaderValid(h));
  atmHeaderInit(h);
  h.R_specific = std::nan(""); // non-finite rejected
  EXPECT_FALSE(atmHeaderValid(h));
}

TEST(AtmHeaderValidation, ExpectedRecordCounts) {
  EXPECT_EQ(atmExpectedRecordCount(AtmModelType::kConstant), 1u);
  EXPECT_EQ(atmExpectedRecordCount(AtmModelType::kExponential), 1u);
  EXPECT_EQ(atmExpectedRecordCount(AtmModelType::kEmpirical), 1u);
  EXPECT_EQ(atmExpectedRecordCount(AtmModelType::kLayered), 0u); // "any positive"
}

TEST(AtmHeaderValidation, BodySizeIsRecordsTimesRecordSize) {
  AtmHeader h{};
  atmHeaderInit(h);
  h.model_type = static_cast<std::uint8_t>(AtmModelType::kLayered);
  h.n_records = 5u;
  EXPECT_EQ(atmBodySize(h), 5u * kAtmRecordSize);
}

/* ----------------------------- Record helpers ----------------------------- */

TEST(AtmRecordHelpers, ConstructFields) {
  const AtmRecord c = atmMakeConstant(1.225, 288.15, 101325.0);
  EXPECT_DOUBLE_EQ(c.f0, 1.225);
  EXPECT_DOUBLE_EQ(c.f1, 288.15);
  EXPECT_DOUBLE_EQ(c.f2, 101325.0);
  EXPECT_DOUBLE_EQ(c.f3, 0.0);

  const AtmRecord e = atmMakeExponential(1.0, 250.0, 8500.0);
  EXPECT_DOUBLE_EQ(e.f2, 8500.0);

  const AtmRecord l = atmMakeLayer(11000.0, 216.65, 22632.06, 0.0);
  EXPECT_DOUBLE_EQ(l.f0, 11000.0);
  EXPECT_DOUBLE_EQ(l.f3, 0.0);
}

/* ----------------------------- Writer guards ----------------------------- */

TEST_F(AtmFileTest, WriterRejectsInvalidHeader) {
  AtmHeader h{};
  atmHeaderInit(h);
  h.magic[0] = 'Z'; // invalid
  AtmWriter w;
  EXPECT_FALSE(w.open(tmpPath("badhdr").c_str(), h));
  EXPECT_FALSE(w.isOpen());
}

TEST_F(AtmFileTest, WriterRejectsRecordCountMismatch) {
  AtmHeader h{};
  atmHeaderInit(h);
  h.model_type = static_cast<std::uint8_t>(AtmModelType::kLayered);
  h.n_records = 2u;
  AtmWriter w;
  ASSERT_TRUE(w.open(tmpPath("mismatch").c_str(), h));
  AtmRecord one{0.0, 288.0, 101325.0, 0.0};
  // Claims 2 records in the header but only offers 1.
  EXPECT_FALSE(w.writeAllRecords(&one, 1));
}

TEST_F(AtmFileTest, WriterRejectsNullRecords) {
  AtmHeader h{};
  atmHeaderInit(h);
  AtmWriter w;
  ASSERT_TRUE(w.open(tmpPath("nullrec").c_str(), h));
  EXPECT_FALSE(w.writeAllRecords(nullptr, 1));
}

TEST_F(AtmFileTest, WriterCloseIsIdempotent) {
  AtmHeader h{};
  atmHeaderInit(h);
  AtmWriter w;
  ASSERT_TRUE(w.open(tmpPath("idem").c_str(), h));
  w.close();
  EXPECT_FALSE(w.isOpen());
  w.close(); // no crash, no-op
  EXPECT_FALSE(w.isOpen());
}

/* ----------------------------- Round trip ----------------------------- */

TEST_F(AtmFileTest, ConstantRoundTrip) {
  const std::string path = tmpPath("const_rt");
  AtmHeader h{};
  atmHeaderInit(h);
  std::strncpy(h.body, "earth", sizeof(h.body) - 1);
  h.spec_hash = 0xABCDEF01u;
  const AtmRecord rec = atmMakeConstant(1.225, 288.15, 101325.0);
  {
    AtmWriter w;
    ASSERT_TRUE(w.open(path.c_str(), h));
    ASSERT_TRUE(w.writeAllRecords(&rec, 1));
  }
  AtmReader r;
  ASSERT_TRUE(r.open(path.c_str()));
  EXPECT_TRUE(atmHeaderValid(r.header()));
  EXPECT_EQ(r.header().n_records, 1u);
  EXPECT_EQ(r.header().spec_hash, 0xABCDEF01u);
  EXPECT_EQ(std::string(r.header().body), "earth");
  AtmRecord got{};
  ASSERT_TRUE(r.readAllRecords(&got, 1));
  EXPECT_DOUBLE_EQ(got.f0, rec.f0);
  EXPECT_DOUBLE_EQ(got.f1, rec.f1);
  EXPECT_DOUBLE_EQ(got.f2, rec.f2);
}

TEST_F(AtmFileTest, LayeredMultiRecordRoundTrip) {
  const std::string path = tmpPath("layered_rt");
  AtmHeader h{};
  atmHeaderInit(h);
  h.model_type = static_cast<std::uint8_t>(AtmModelType::kLayered);
  h.n_records = 3u;
  std::vector<AtmRecord> recs = {
      atmMakeLayer(0.0, 288.15, 101325.0, -0.0065),
      atmMakeLayer(11000.0, 216.65, 22632.06, 0.0),
      atmMakeLayer(20000.0, 216.65, 5474.889, 0.001),
  };
  {
    AtmWriter w;
    ASSERT_TRUE(w.open(path.c_str(), h));
    ASSERT_TRUE(w.writeAllRecords(recs.data(), recs.size()));
  }
  AtmReader r;
  ASSERT_TRUE(r.open(path.c_str()));
  ASSERT_EQ(r.header().n_records, 3u);
  std::vector<AtmRecord> got(3);
  ASSERT_TRUE(r.readAllRecords(got.data(), got.size()));
  for (std::size_t i = 0; i < recs.size(); ++i) {
    EXPECT_DOUBLE_EQ(got[i].f0, recs[i].f0) << "record " << i;
    EXPECT_DOUBLE_EQ(got[i].f3, recs[i].f3) << "record " << i;
  }
}

/* ----------------------------- Reader rejects malformed files ----------------------------- */

TEST_F(AtmFileTest, ReaderRejectsMissingFile) {
  AtmReader r;
  EXPECT_FALSE(r.open("/tmp/atm_uTest_does_not_exist_qwerty.atm"));
  EXPECT_FALSE(r.isOpen());
}

TEST_F(AtmFileTest, ReaderRejectsTruncatedHeader) {
  // Only 10 bytes -- shorter than a 64-byte header.
  AtmHeader h{};
  atmHeaderInit(h);
  const std::string path = writeRaw("trunchdr", &h, 10);
  ASSERT_FALSE(path.empty());
  AtmReader r;
  EXPECT_FALSE(r.open(path.c_str()));
}

TEST_F(AtmFileTest, ReaderRejectsBadMagicFile) {
  AtmHeader h{};
  atmHeaderInit(h);
  h.magic[0] = 'B';
  const std::string path = writeRaw("badmagic", &h, sizeof(h));
  ASSERT_FALSE(path.empty());
  AtmReader r;
  EXPECT_FALSE(r.open(path.c_str()));
}

TEST_F(AtmFileTest, ReaderRejectsBadVersionFile) {
  AtmHeader h{};
  atmHeaderInit(h);
  h.version = 999u;
  const std::string path = writeRaw("badver", &h, sizeof(h));
  ASSERT_FALSE(path.empty());
  AtmReader r;
  EXPECT_FALSE(r.open(path.c_str()));
}

TEST_F(AtmFileTest, ReaderRejectsTruncatedBody) {
  // Valid header announcing 3 layered records, but the body is short.
  AtmHeader h{};
  atmHeaderInit(h);
  h.model_type = static_cast<std::uint8_t>(AtmModelType::kLayered);
  h.n_records = 3u;
  std::vector<unsigned char> bytes(sizeof(h) + kAtmRecordSize); // only 1 record present
  std::memcpy(bytes.data(), &h, sizeof(h));
  const std::string path = writeRaw("truncbody", bytes.data(), bytes.size());
  ASSERT_FALSE(path.empty());

  AtmReader r;
  ASSERT_TRUE(r.open(path.c_str())); // header is valid
  std::vector<AtmRecord> got(3);
  EXPECT_FALSE(r.readAllRecords(got.data(), got.size())); // body short -> false
}

TEST_F(AtmFileTest, ReaderRejectsRecordCountMismatch) {
  const std::string path = tmpPath("rdmismatch");
  AtmHeader h{};
  atmHeaderInit(h);
  h.model_type = static_cast<std::uint8_t>(AtmModelType::kLayered);
  h.n_records = 2u;
  std::vector<AtmRecord> recs = {atmMakeLayer(0.0, 288.15, 101325.0, -0.0065),
                                 atmMakeLayer(11000.0, 216.65, 22632.06, 0.0)};
  {
    AtmWriter w;
    ASSERT_TRUE(w.open(path.c_str(), h));
    ASSERT_TRUE(w.writeAllRecords(recs.data(), recs.size()));
  }
  AtmReader r;
  ASSERT_TRUE(r.open(path.c_str()));
  AtmRecord got{};
  // header says 2 but caller asks for 1 -> rejected.
  EXPECT_FALSE(r.readAllRecords(&got, 1));
}

TEST_F(AtmFileTest, ReaderRejectsNullDestination) {
  const std::string path = tmpPath("rdnull");
  AtmHeader h{};
  atmHeaderInit(h);
  const AtmRecord rec = atmMakeConstant(1.0, 288.0, 101325.0);
  {
    AtmWriter w;
    ASSERT_TRUE(w.open(path.c_str(), h));
    ASSERT_TRUE(w.writeAllRecords(&rec, 1));
  }
  AtmReader r;
  ASSERT_TRUE(r.open(path.c_str()));
  EXPECT_FALSE(r.readAllRecords(nullptr, 1));
}

} // namespace
