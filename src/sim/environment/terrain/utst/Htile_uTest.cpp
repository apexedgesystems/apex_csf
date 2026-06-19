/**
 * @file Htile_uTest.cpp
 * @brief Direct unit tests for the `.htile` wire-format primitives in
 *        Htile.hpp: HtileReader / HtileWriter and the free helpers
 *        (htileHeaderValid, htileBodySize, htileSampleSize, htileMagicValid,
 *        htileHeaderInit).
 *
 * These exercise the reader/writer in isolation from the HtileTile consumer,
 * covering: write-then-read round trip, magic / version / bounds / radius /
 * scale validation, truncated header + body handling, readAll / writeAll
 * size-mismatch rejection, float32 sample sizing, and the B2 out-of-int16
 * void_value case (rejected by the HtileTile consumer, but here we confirm
 * the writer/reader themselves still accept it as a structurally valid
 * header so that the consumer-level rejection is the single source of truth).
 */

#include "src/sim/environment/terrain/inc/Htile.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using sim::environment::terrain::htileBodySize;
using sim::environment::terrain::HtileEndian;
using sim::environment::terrain::HtileHeader;
using sim::environment::terrain::htileHeaderInit;
using sim::environment::terrain::htileHeaderValid;
using sim::environment::terrain::htileMagicValid;
using sim::environment::terrain::HtileReader;
using sim::environment::terrain::HtileRowOrder;
using sim::environment::terrain::htileSampleSize;
using sim::environment::terrain::HtileSampleType;
using sim::environment::terrain::HtileWriter;
using sim::environment::terrain::kHtileHeaderSize;
using sim::environment::terrain::kHtileMagic;
using sim::environment::terrain::kHtileVersion;

namespace {

/// Build a default-valid 3x2 int16 header.
HtileHeader makeValidHeader() {
  HtileHeader hdr{};
  htileHeaderInit(hdr);
  std::strncpy(hdr.body, "test", sizeof(hdr.body) - 1);
  std::strncpy(hdr.ref_surface, "sphere", sizeof(hdr.ref_surface) - 1);
  hdr.ref_radius_m = 6.0e6;
  hdr.lat_min_deg = -1.0;
  hdr.lat_max_deg = 1.0;
  hdr.lon_min_deg = -2.0;
  hdr.lon_max_deg = 2.0;
  hdr.dim_lat = 3;
  hdr.dim_lon = 2;
  return hdr;
}

class HtileTest : public ::testing::Test {
protected:
  void TearDown() override {
    for (const auto& p : created_) {
      std::error_code ec;
      std::filesystem::remove(p, ec);
    }
    created_.clear();
  }
  std::string tmp(const char* hint) {
    static int counter = 0;
    ++counter;
    const std::string PATH = "/tmp/apex_htile_uTest_" + std::string(hint) + "_" +
                             std::to_string(::getpid()) + "_" + std::to_string(counter) + ".htile";
    created_.push_back(PATH);
    return PATH;
  }

  /// Write `bytes` raw to `path` (used to fabricate truncated/corrupt files).
  static void writeRaw(const std::string& path, const void* data, std::size_t bytes) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    if (bytes != 0) {
      ASSERT_EQ(std::fwrite(data, 1, bytes, f), bytes);
    }
    std::fclose(f);
  }

private:
  std::vector<std::string> created_;
};

/* ----------------------------- Helpers ----------------------------- */

TEST(HtileHelpers, SampleSize) {
  EXPECT_EQ(htileSampleSize(HtileSampleType::kInt16), 2u);
  EXPECT_EQ(htileSampleSize(HtileSampleType::kFloat32), 4u);
}

TEST(HtileHelpers, BodySizeInt16) {
  HtileHeader hdr{};
  htileHeaderInit(hdr);
  hdr.dim_lat = 4;
  hdr.dim_lon = 5;
  hdr.sample_type = static_cast<std::uint8_t>(HtileSampleType::kInt16);
  EXPECT_EQ(htileBodySize(hdr), 4u * 5u * 2u);
}

TEST(HtileHelpers, BodySizeFloat32) {
  HtileHeader hdr{};
  htileHeaderInit(hdr);
  hdr.dim_lat = 4;
  hdr.dim_lon = 5;
  hdr.sample_type = static_cast<std::uint8_t>(HtileSampleType::kFloat32);
  EXPECT_EQ(htileBodySize(hdr), 4u * 5u * 4u);
}

TEST(HtileHelpers, InitDefaults) {
  HtileHeader hdr{};
  std::memset(&hdr, 0xAB, sizeof(hdr));
  htileHeaderInit(hdr);
  EXPECT_TRUE(htileMagicValid(hdr));
  EXPECT_EQ(hdr.version, kHtileVersion);
  EXPECT_EQ(hdr.row_order, static_cast<std::uint8_t>(HtileRowOrder::kNorthToSouth));
  EXPECT_EQ(hdr.sample_type, static_cast<std::uint8_t>(HtileSampleType::kInt16));
  EXPECT_EQ(hdr.endianness, static_cast<std::uint8_t>(HtileEndian::kLittle));
  EXPECT_DOUBLE_EQ(hdr.scale_m_per_dn, 1.0);
}

/* ----------------------------- htileHeaderValid ----------------------------- */

TEST(HtileHeaderValid, AcceptsValid) { EXPECT_TRUE(htileHeaderValid(makeValidHeader())); }

TEST(HtileHeaderValid, RejectsBadMagic) {
  HtileHeader hdr = makeValidHeader();
  hdr.magic[0] = 'X';
  EXPECT_FALSE(htileMagicValid(hdr));
  EXPECT_FALSE(htileHeaderValid(hdr));
}

TEST(HtileHeaderValid, RejectsWrongVersion) {
  HtileHeader hdr = makeValidHeader();
  hdr.version = kHtileVersion + 1u;
  EXPECT_FALSE(htileHeaderValid(hdr));
}

TEST(HtileHeaderValid, RejectsZeroDims) {
  HtileHeader hdr = makeValidHeader();
  hdr.dim_lat = 0;
  EXPECT_FALSE(htileHeaderValid(hdr));
  hdr = makeValidHeader();
  hdr.dim_lon = 0;
  EXPECT_FALSE(htileHeaderValid(hdr));
}

TEST(HtileHeaderValid, RejectsNonFiniteBounds) {
  HtileHeader hdr = makeValidHeader();
  hdr.lat_max_deg = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(htileHeaderValid(hdr));
  hdr = makeValidHeader();
  hdr.lon_min_deg = std::numeric_limits<double>::infinity();
  EXPECT_FALSE(htileHeaderValid(hdr));
}

TEST(HtileHeaderValid, RejectsMisorderedBounds) {
  HtileHeader hdr = makeValidHeader();
  hdr.lat_min_deg = 10.0;
  hdr.lat_max_deg = -10.0;
  EXPECT_FALSE(htileHeaderValid(hdr));
  hdr = makeValidHeader();
  hdr.lon_min_deg = 5.0;
  hdr.lon_max_deg = 5.0; // equal is not ordered (strict)
  EXPECT_FALSE(htileHeaderValid(hdr));
}

TEST(HtileHeaderValid, RejectsNonPositiveRefRadius) {
  HtileHeader hdr = makeValidHeader();
  hdr.ref_radius_m = 0.0;
  EXPECT_FALSE(htileHeaderValid(hdr));
  hdr.ref_radius_m = -1.0;
  EXPECT_FALSE(htileHeaderValid(hdr));
}

TEST(HtileHeaderValid, RejectsNonPositiveScale) {
  HtileHeader hdr = makeValidHeader();
  hdr.scale_m_per_dn = 0.0;
  EXPECT_FALSE(htileHeaderValid(hdr));
  hdr.scale_m_per_dn = -0.5;
  EXPECT_FALSE(htileHeaderValid(hdr));
}

/* ----------------------------- Round trip ----------------------------- */

TEST_F(HtileTest, WriteThenReadRoundTrip) {
  const std::string PATH = tmp("roundtrip");
  HtileHeader hdr = makeValidHeader();
  const std::size_t COUNT = static_cast<std::size_t>(hdr.dim_lat) * hdr.dim_lon;
  std::vector<std::int16_t> src(COUNT);
  for (std::size_t i = 0; i < COUNT; ++i) {
    src[i] = static_cast<std::int16_t>(i * 7 - 3);
  }

  {
    HtileWriter w;
    ASSERT_TRUE(w.open(PATH.c_str(), hdr));
    EXPECT_TRUE(w.isOpen());
    ASSERT_TRUE(w.writeAllSamples(src.data(), src.size() * sizeof(std::int16_t)));
    w.close();
    EXPECT_FALSE(w.isOpen());
  }

  HtileReader r;
  ASSERT_TRUE(r.open(PATH.c_str()));
  EXPECT_TRUE(r.isOpen());
  EXPECT_EQ(r.header().dim_lat, hdr.dim_lat);
  EXPECT_EQ(r.header().dim_lon, hdr.dim_lon);
  EXPECT_DOUBLE_EQ(r.header().ref_radius_m, hdr.ref_radius_m);
  EXPECT_STREQ(r.header().body, "test");

  std::vector<std::int16_t> dst(COUNT, 0);
  ASSERT_TRUE(r.readAllSamples(dst.data(), dst.size() * sizeof(std::int16_t)));
  EXPECT_EQ(src, dst);
}

TEST_F(HtileTest, WriterRejectsInvalidHeader) {
  const std::string PATH = tmp("badhdr");
  HtileHeader hdr = makeValidHeader();
  hdr.ref_radius_m = -1.0; // invalid
  HtileWriter w;
  EXPECT_FALSE(w.open(PATH.c_str(), hdr));
  EXPECT_FALSE(w.isOpen());
}

/* ----------------------------- writeAll / readAll mismatch ----------------------------- */

TEST_F(HtileTest, WriteAllSizeMismatchRejected) {
  const std::string PATH = tmp("wsize");
  HtileHeader hdr = makeValidHeader();
  HtileWriter w;
  ASSERT_TRUE(w.open(PATH.c_str(), hdr));
  std::vector<std::int16_t> wrong(htileBodySize(hdr) / sizeof(std::int16_t) + 1);
  EXPECT_FALSE(w.writeAllSamples(wrong.data(), wrong.size() * sizeof(std::int16_t)));
}

TEST_F(HtileTest, ReadAllSizeMismatchRejected) {
  const std::string PATH = tmp("rsize");
  HtileHeader hdr = makeValidHeader();
  const std::size_t COUNT = static_cast<std::size_t>(hdr.dim_lat) * hdr.dim_lon;
  std::vector<std::int16_t> src(COUNT, 1);
  {
    HtileWriter w;
    ASSERT_TRUE(w.open(PATH.c_str(), hdr));
    ASSERT_TRUE(w.writeAllSamples(src.data(), src.size() * sizeof(std::int16_t)));
  }
  HtileReader r;
  ASSERT_TRUE(r.open(PATH.c_str()));
  std::vector<std::int16_t> dst(COUNT + 5);
  EXPECT_FALSE(r.readAllSamples(dst.data(), dst.size() * sizeof(std::int16_t)));
}

/* ----------------------------- Truncated / corrupt files ----------------------------- */

TEST_F(HtileTest, MissingFileRejected) {
  HtileReader r;
  EXPECT_FALSE(r.open("/tmp/apex_htile_does_not_exist_xyz.htile"));
  EXPECT_FALSE(r.isOpen());
}

TEST_F(HtileTest, TruncatedHeaderRejected) {
  const std::string PATH = tmp("trunchdr");
  HtileHeader hdr = makeValidHeader();
  // Write only half the header.
  writeRaw(PATH, &hdr, kHtileHeaderSize / 2);
  HtileReader r;
  EXPECT_FALSE(r.open(PATH.c_str()));
}

TEST_F(HtileTest, BadMagicFileRejected) {
  const std::string PATH = tmp("badmagic");
  HtileHeader hdr = makeValidHeader();
  hdr.magic[0] = 'Z';
  writeRaw(PATH, &hdr, sizeof(hdr));
  HtileReader r;
  EXPECT_FALSE(r.open(PATH.c_str()));
}

TEST_F(HtileTest, TruncatedBodyHandledGracefully) {
  const std::string PATH = tmp("truncbody");
  HtileHeader hdr = makeValidHeader();
  const std::size_t COUNT = static_cast<std::size_t>(hdr.dim_lat) * hdr.dim_lon;
  // Valid header + a body that is one sample short.
  std::vector<std::int16_t> shortBody(COUNT - 1, 9);
  std::FILE* f = std::fopen(PATH.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(std::fwrite(&hdr, 1, sizeof(hdr), f), sizeof(hdr));
  ASSERT_EQ(std::fwrite(shortBody.data(), sizeof(std::int16_t), shortBody.size(), f),
            shortBody.size());
  std::fclose(f);

  HtileReader r;
  ASSERT_TRUE(r.open(PATH.c_str())); // header is valid
  std::vector<std::int16_t> dst(COUNT, 0);
  // Body read of the full expected size must fail on the short read.
  EXPECT_FALSE(r.readAllSamples(dst.data(), dst.size() * sizeof(std::int16_t)));
}

/* ----------------------------- float32 sizing ----------------------------- */

TEST_F(HtileTest, Float32RoundTripBytes) {
  const std::string PATH = tmp("f32");
  HtileHeader hdr = makeValidHeader();
  hdr.sample_type = static_cast<std::uint8_t>(HtileSampleType::kFloat32);
  EXPECT_TRUE(htileHeaderValid(hdr));
  const std::size_t COUNT = static_cast<std::size_t>(hdr.dim_lat) * hdr.dim_lon;
  EXPECT_EQ(htileBodySize(hdr), COUNT * 4u);

  std::vector<float> src(COUNT);
  for (std::size_t i = 0; i < COUNT; ++i) {
    src[i] = static_cast<float>(i) * 1.5f;
  }
  {
    HtileWriter w;
    ASSERT_TRUE(w.open(PATH.c_str(), hdr));
    ASSERT_TRUE(w.writeAllSamples(src.data(), src.size() * sizeof(float)));
  }
  HtileReader r;
  ASSERT_TRUE(r.open(PATH.c_str()));
  std::vector<float> dst(COUNT, 0.0f);
  ASSERT_TRUE(r.readAllSamples(dst.data(), dst.size() * sizeof(float)));
  EXPECT_EQ(src, dst);
}

/* ----------------------------- B2: out-of-int16 void_value ----------------------------- */

TEST_F(HtileTest, OutOfInt16VoidValueIsStructurallyValidHeader) {
  // The wire-format header itself does not constrain void_value to int16
  // range (the field is int32). htileHeaderValid / the reader/writer accept
  // it; it is the int16 HtileTile *consumer* that rejects it (see
  // HtileTile_uTest's B2 test). This pins the layering so the rejection lives
  // in exactly one place.
  HtileHeader hdr = makeValidHeader();
  hdr.void_value = 70000; // > int16 max (32767)
  EXPECT_TRUE(htileHeaderValid(hdr));

  const std::string PATH = tmp("b2void");
  const std::size_t COUNT = static_cast<std::size_t>(hdr.dim_lat) * hdr.dim_lon;
  std::vector<std::int16_t> src(COUNT, 0);
  {
    HtileWriter w;
    ASSERT_TRUE(w.open(PATH.c_str(), hdr));
    EXPECT_TRUE(w.writeAllSamples(src.data(), src.size() * sizeof(std::int16_t)));
  } // close + flush before reading
  HtileReader r;
  EXPECT_TRUE(r.open(PATH.c_str()));
  EXPECT_EQ(r.header().void_value, 70000);
}

} // namespace
