/**
 * @file HtileTile_uTest.cpp
 * @brief Tests for the HtileTile terrain consumer.
 *
 * Coverage:
 *   - load(): missing path -> ERROR_DATA_PATH_INVALID, valid synthetic
 *     htile loads -> SUCCESS.
 *   - elevationAt: corner samples returned exactly, bilinear midpoint
 *     interpolates in both column and row directions, out-of-coverage ->
 *     WARN_OUTSIDE_COVERAGE, void cells -> WARN_VOID_DATA (H untouched).
 *   - elevationAtEcef: spherical conversion produces same answer as
 *     elevationAt at the corresponding lat/lon.
 *   - Coverage bounds + resolution accessors track header values.
 *   - close() resets state.
 *   - Round-trip with `scale_m_per_dn != 1.0` (Moon-style).
 */

#include "src/sim/environment/terrain/inc/Htile.hpp"
#include "src/sim/environment/terrain/inc/HtileTile.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using sim::environment::terrain::HtileHeader;
using sim::environment::terrain::htileHeaderInit;
using sim::environment::terrain::HtileTile;
using sim::environment::terrain::HtileWriter;
using sim::environment::terrain::Status;

namespace {

constexpr double K_DEG_PER_RAD = 57.295779513082320876798154814105;

/// Build a 4x4 synthetic htile at `path` with samples set per `gen(row, col)`.
template <typename Fn> void writeSyntheticTile(const std::string& path, double scale, Fn gen) {
  HtileHeader hdr{};
  htileHeaderInit(hdr);
  std::strncpy(hdr.body, "alpha", sizeof(hdr.body) - 1);
  std::strncpy(hdr.ref_surface, "sphere", sizeof(hdr.ref_surface) - 1);
  hdr.ref_radius_m = 6e6;
  hdr.lat_min_deg = 0.0;
  hdr.lat_max_deg = 1.0;
  hdr.lon_min_deg = 0.0;
  hdr.lon_max_deg = 1.0;
  hdr.dim_lat = 4;
  hdr.dim_lon = 4;
  hdr.scale_m_per_dn = scale;

  std::vector<std::int16_t> samples(static_cast<std::size_t>(4) * 4);
  for (std::uint32_t r = 0; r < 4; ++r) {
    for (std::uint32_t c = 0; c < 4; ++c) {
      samples[r * 4 + c] = gen(r, c);
    }
  }
  HtileWriter w;
  ASSERT_TRUE(w.open(path.c_str(), hdr));
  ASSERT_TRUE(w.writeAllSamples(samples.data(), samples.size() * sizeof(std::int16_t)));
}

class HtileTileTest : public ::testing::Test {
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
    const std::string PATH = "/tmp/apex_htiletile_uTest_" + std::string(hint) + "_" +
                             std::to_string(::getpid()) + "_" + std::to_string(counter) + ".htile";
    created_.push_back(PATH);
    return PATH;
  }

private:
  std::vector<std::string> created_;
};

/* ----------------------------- load() ----------------------------- */

TEST_F(HtileTileTest, MissingFileFails) {
  HtileTile t;
  EXPECT_EQ(t.load("/tmp/apex_htiletile_does_not_exist.htile"), Status::ERROR_DATA_PATH_INVALID);
  EXPECT_FALSE(t.isLoaded());
}

TEST_F(HtileTileTest, SyntheticLoads) {
  const std::string PATH = tmp("load");
  writeSyntheticTile(PATH, 1.0, [](std::uint32_t r, std::uint32_t c) -> std::int16_t {
    return static_cast<std::int16_t>(100 * r + c);
  });
  HtileTile t;
  ASSERT_EQ(t.load(PATH), Status::SUCCESS);
  EXPECT_TRUE(t.isLoaded());
  EXPECT_EQ(t.header().dim_lat, 4u);
  EXPECT_EQ(t.header().dim_lon, 4u);
  EXPECT_DOUBLE_EQ(t.header().lat_min_deg, 0.0);
  EXPECT_DOUBLE_EQ(t.header().lat_max_deg, 1.0);
  EXPECT_GT(t.resolutionMeters(), 0.0);
}

/* ----------------------------- Elevation queries ----------------------------- */

TEST_F(HtileTileTest, CornerSamplesExact) {
  // Set up a tile where each cell has a unique value, so we can identify
  // which cell got hit.
  const std::string PATH = tmp("corner");
  writeSyntheticTile(PATH, 1.0, [](std::uint32_t r, std::uint32_t c) -> std::int16_t {
    return static_cast<std::int16_t>(r * 10 + c);
  });
  HtileTile t;
  ASSERT_EQ(t.load(PATH), Status::SUCCESS);

  // Sample at the NW corner (lat_max, lon_min) -> row 0, col 0.
  double H = -999.0;
  ASSERT_EQ(t.elevationAt(1.0 / K_DEG_PER_RAD, 0.0, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 0.0); // gen(0, 0) = 0

  // Sample at the SE corner (lat_min, lon_max) -> row 3, col 3.
  ASSERT_EQ(t.elevationAt(0.0, 1.0 / K_DEG_PER_RAD, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 33.0); // gen(3, 3) = 33

  // NE corner (lat_max, lon_max) -> row 0, col 3.
  ASSERT_EQ(t.elevationAt(1.0 / K_DEG_PER_RAD, 1.0 / K_DEG_PER_RAD, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 3.0);

  // SW corner (lat_min, lon_min) -> row 3, col 0.
  ASSERT_EQ(t.elevationAt(0.0, 0.0, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 30.0);
}

TEST_F(HtileTileTest, BilinearMidpoint) {
  // 4x4 with samples = 0 in NW half, 100 in SE half. Lat/lon midpoint
  // should bilinear-interpolate between corners.
  const std::string PATH = tmp("bilinear");
  writeSyntheticTile(PATH, 1.0, [](std::uint32_t, std::uint32_t c) -> std::int16_t {
    // West cols (c=0,1) -> 0, East cols (c=2,3) -> 100. Bilinear at the
    // exact column midpoint should be 50.
    return (c >= 2) ? static_cast<std::int16_t>(100) : static_cast<std::int16_t>(0);
  });
  HtileTile t;
  ASSERT_EQ(t.load(PATH), Status::SUCCESS);
  // Query at the exact center of the tile in lon (lat doesn't matter
  // since rows are identical).
  double H = 0.0;
  ASSERT_EQ(t.elevationAt(0.5 / K_DEG_PER_RAD, 0.5 / K_DEG_PER_RAD, H), Status::SUCCESS);
  EXPECT_NEAR(H, 50.0, 1e-9);
}

TEST_F(HtileTileTest, BilinearRowMidpoint) {
  // T4: samples vary in the LATITUDE (row) direction only. Row 0 is the
  // north edge (htile canonical N->S): rows 0,1 -> 0 m, rows 2,3 -> 100 m.
  // Querying the exact row midpoint (between the band of 0s and 100s) must
  // interpolate to 50 m. A flipped-row-order regression (treating row 0 as
  // the south edge) would still bisect a symmetric column pattern but NOT
  // this asymmetric north/south split -- this is the column-only suite's gap.
  const std::string PATH = tmp("bilinear_row");
  writeSyntheticTile(PATH, 1.0, [](std::uint32_t r, std::uint32_t) -> std::int16_t {
    // North rows (r=0,1) -> 0, south rows (r=2,3) -> 100. Bilinear at the
    // exact row midpoint should be 50.
    return (r >= 2) ? static_cast<std::int16_t>(100) : static_cast<std::int16_t>(0);
  });
  HtileTile t;
  ASSERT_EQ(t.load(PATH), Status::SUCCESS);
  // Query at the exact center of the tile in lat (lon doesn't matter since
  // columns are identical within each row).
  double H = 0.0;
  ASSERT_EQ(t.elevationAt(0.5 / K_DEG_PER_RAD, 0.5 / K_DEG_PER_RAD, H), Status::SUCCESS);
  EXPECT_NEAR(H, 50.0, 1e-9);

  // Sanity: a point clearly in the north half reads low, south half reads
  // high. This is what pins down the row orientation against a flip.
  double H_NORTH = 0.0;
  double H_SOUTH = 0.0;
  ASSERT_EQ(t.elevationAt(0.95 / K_DEG_PER_RAD, 0.5 / K_DEG_PER_RAD, H_NORTH), Status::SUCCESS);
  ASSERT_EQ(t.elevationAt(0.05 / K_DEG_PER_RAD, 0.5 / K_DEG_PER_RAD, H_SOUTH), Status::SUCCESS);
  EXPECT_LT(H_NORTH, 50.0); // near lat_max (north) -> rows ~0 -> low
  EXPECT_GT(H_SOUTH, 50.0); // near lat_min (south) -> rows ~3 -> high
}

TEST_F(HtileTileTest, OutsideCoverageFails) {
  const std::string PATH = tmp("oob");
  writeSyntheticTile(PATH, 1.0, [](std::uint32_t, std::uint32_t) -> std::int16_t { return 5; });
  HtileTile t;
  ASSERT_EQ(t.load(PATH), Status::SUCCESS);
  double H = 0.0;
  EXPECT_EQ(t.elevationAt(2.0 / K_DEG_PER_RAD, 0.5 / K_DEG_PER_RAD, H),
            Status::WARN_OUTSIDE_COVERAGE); // lat > max
  EXPECT_EQ(t.elevationAt(0.5 / K_DEG_PER_RAD, -1.0 / K_DEG_PER_RAD, H),
            Status::WARN_OUTSIDE_COVERAGE); // lon < min
}

TEST_F(HtileTileTest, VoidCellRejected) {
  const std::string PATH = tmp("void");
  writeSyntheticTile(PATH, 1.0, [](std::uint32_t r, std::uint32_t c) -> std::int16_t {
    return (r == 0 && c == 0) ? static_cast<std::int16_t>(-32768) : static_cast<std::int16_t>(10);
  });
  HtileTile t;
  ASSERT_EQ(t.load(PATH), Status::SUCCESS);
  double H = -999.0;
  // NW corner samples the void cell.
  EXPECT_EQ(t.elevationAt(1.0 / K_DEG_PER_RAD, 0.0, H), Status::WARN_VOID_DATA);
  EXPECT_DOUBLE_EQ(H, -999.0); // H left unmodified on non-SUCCESS
}

TEST_F(HtileTileTest, ScalingFactorAppliedToHeights) {
  // Moon-style: scale_m_per_dn = 0.5, so a sample of 100 means 50 m.
  const std::string PATH = tmp("scaled");
  writeSyntheticTile(PATH, 0.5, [](std::uint32_t, std::uint32_t) -> std::int16_t { return 100; });
  HtileTile t;
  ASSERT_EQ(t.load(PATH), Status::SUCCESS);
  double H = 0.0;
  ASSERT_EQ(t.elevationAt(0.5 / K_DEG_PER_RAD, 0.5 / K_DEG_PER_RAD, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 50.0);
}

TEST_F(HtileTileTest, OutOfInt16VoidValueRejectedAtLoad) {
  // B2: void_value is int32 on disk; samples are int16. A void_value that
  // cannot be represented as int16 could never match a sample, silently
  // disabling void rejection -- the consumer must reject such a tile as a
  // format error at load time. Build a structurally valid header whose
  // void_value is out of int16 range and confirm load() fails.
  const std::string PATH = tmp("b2void");
  HtileHeader hdr{};
  htileHeaderInit(hdr);
  std::strncpy(hdr.body, "alpha", sizeof(hdr.body) - 1);
  std::strncpy(hdr.ref_surface, "sphere", sizeof(hdr.ref_surface) - 1);
  hdr.ref_radius_m = 6e6;
  hdr.lat_min_deg = 0.0;
  hdr.lat_max_deg = 1.0;
  hdr.lon_min_deg = 0.0;
  hdr.lon_max_deg = 1.0;
  hdr.dim_lat = 4;
  hdr.dim_lon = 4;
  hdr.void_value = 100000; // > int16 max (32767)

  std::vector<std::int16_t> samples(16, 7);
  HtileWriter w;
  ASSERT_TRUE(w.open(PATH.c_str(), hdr));
  ASSERT_TRUE(w.writeAllSamples(samples.data(), samples.size() * sizeof(std::int16_t)));
  w.close();

  HtileTile t;
  EXPECT_EQ(t.load(PATH), Status::ERROR_FILE_FORMAT_INVALID);
  EXPECT_FALSE(t.isLoaded());
}

/* ----------------------------- Coverage bounds ----------------------------- */

TEST_F(HtileTileTest, CoverageBoundsTrackHeader) {
  const std::string PATH = tmp("bounds");
  writeSyntheticTile(PATH, 1.0, [](std::uint32_t, std::uint32_t) -> std::int16_t { return 0; });
  HtileTile t;
  ASSERT_EQ(t.load(PATH), Status::SUCCESS);
  EXPECT_NEAR(t.minLatRad(), 0.0 / K_DEG_PER_RAD, 1e-12);
  EXPECT_NEAR(t.maxLatRad(), 1.0 / K_DEG_PER_RAD, 1e-12);
  EXPECT_NEAR(t.minLonRad(), 0.0 / K_DEG_PER_RAD, 1e-12);
  EXPECT_NEAR(t.maxLonRad(), 1.0 / K_DEG_PER_RAD, 1e-12);
  EXPECT_TRUE(t.isInCoverage(0.5 / K_DEG_PER_RAD, 0.5 / K_DEG_PER_RAD));
  EXPECT_FALSE(t.isInCoverage(2.0 / K_DEG_PER_RAD, 0.5 / K_DEG_PER_RAD));
}

/* ----------------------------- Wraparound longitude (B4) ----------------------------- */

TEST_F(HtileTileTest, WraparoundLongitudeAccepted) {
  // B4: a tile declaring a 0..360 east-longitude window must accept a
  // [-180, 180] query longitude (the form elevationAtEcef's atan2 produces)
  // by normalizing modulo 360 before the bounds check.
  const std::string PATH = tmp("wrap");
  HtileHeader hdr{};
  htileHeaderInit(hdr);
  std::strncpy(hdr.body, "alpha", sizeof(hdr.body) - 1);
  std::strncpy(hdr.ref_surface, "sphere", sizeof(hdr.ref_surface) - 1);
  hdr.ref_radius_m = 6e6;
  hdr.lat_min_deg = -1.0;
  hdr.lat_max_deg = 1.0;
  hdr.lon_min_deg = 0.0;
  hdr.lon_max_deg = 360.0;
  hdr.dim_lat = 4;
  hdr.dim_lon = 4;
  // Vary by column so a longitude shift changes the value: col index maps to
  // increasing east longitude.
  std::vector<std::int16_t> samples(16);
  for (std::uint32_t r = 0; r < 4; ++r) {
    for (std::uint32_t c = 0; c < 4; ++c) {
      samples[r * 4 + c] = static_cast<std::int16_t>(c * 10);
    }
  }
  HtileWriter w;
  ASSERT_TRUE(w.open(PATH.c_str(), hdr));
  ASSERT_TRUE(w.writeAllSamples(samples.data(), samples.size() * sizeof(std::int16_t)));
  w.close();

  HtileTile t;
  ASSERT_EQ(t.load(PATH), Status::SUCCESS);

  // -10 deg should normalize to 350 deg; both must succeed and agree.
  double H_NEG = 0.0;
  double H_350 = 0.0;
  ASSERT_EQ(t.elevationAt(0.0, -10.0 / K_DEG_PER_RAD, H_NEG), Status::SUCCESS);
  ASSERT_EQ(t.elevationAt(0.0, 350.0 / K_DEG_PER_RAD, H_350), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H_NEG, H_350);

  // isInCoverage honors the same normalization.
  EXPECT_TRUE(t.isInCoverage(0.0, -10.0 / K_DEG_PER_RAD));
  EXPECT_TRUE(t.isInCoverage(0.0, -180.0 / K_DEG_PER_RAD));
}

/* ----------------------------- ECEF query ----------------------------- */

TEST_F(HtileTileTest, EcefQueryMatchesGeodetic) {
  const std::string PATH = tmp("ecef");
  writeSyntheticTile(PATH, 1.0, [](std::uint32_t r, std::uint32_t c) -> std::int16_t {
    return static_cast<std::int16_t>(r * 10 + c);
  });
  HtileTile t;
  ASSERT_EQ(t.load(PATH), Status::SUCCESS);

  // Pick a target lat/lon in coverage; build the ECEF for it via spherical
  // assumption with our tile's reference radius.
  const double LAT = 0.5 / K_DEG_PER_RAD;
  const double LON = 0.5 / K_DEG_PER_RAD;
  const double R = t.header().ref_radius_m;
  const double EC[3] = {R * std::cos(LAT) * std::cos(LON), R * std::cos(LAT) * std::sin(LON),
                        R * std::sin(LAT)};
  double H_GEO = 0.0;
  double H_ECEF = 0.0;
  ASSERT_EQ(t.elevationAt(LAT, LON, H_GEO), Status::SUCCESS);
  ASSERT_EQ(t.elevationAtEcef(EC, H_ECEF), Status::SUCCESS);
  EXPECT_NEAR(H_GEO, H_ECEF, 1e-9);
}

/* ----------------------------- close() ----------------------------- */

TEST_F(HtileTileTest, CloseResets) {
  const std::string PATH = tmp("close");
  writeSyntheticTile(PATH, 1.0, [](std::uint32_t, std::uint32_t) -> std::int16_t { return 1; });
  HtileTile t;
  ASSERT_EQ(t.load(PATH), Status::SUCCESS);
  EXPECT_TRUE(t.isLoaded());
  t.close();
  EXPECT_FALSE(t.isLoaded());
  EXPECT_DOUBLE_EQ(t.resolutionMeters(), 0.0);
}

} // namespace
