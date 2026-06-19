/**
 * @file 02_HorizonRealSrtm_dTest.cpp
 * @brief Ground-truth integration test: convert real SRTM N39W106.hgt
 *        through horizon's CLI -> htile, load via apex's HtileTile,
 *        verify elevations at well-known Colorado landmarks against
 *        USGS-published values.
 *
 * Why this matters:
 *   The synthetic and bit-exact round-trip unit tests prove the bytes survive
 *   the pipeline intact. They do NOT prove the coordinate conventions, row
 *   order, lat/lon -> sample mapping, or bilinear interpolation actually map
 *   onto ground truth. This dtest closes that gap.
 *
 * Tile coverage:  39-40 N, 105-106 W (Front Range west of Denver, CO).
 *
 * Tolerance reasoning:
 *   SRTM1 is 30 m horizontal sampling. Documented vertical accuracy:
 *   absolute +-16 m at 90% confidence. C-band radar slightly under-reports
 *   bare-rock summits because of canopy bias on adjacent samples used for
 *   bilinear lookup. Tolerance is +-50 m which gives comfortable margin
 *   over both effects without becoming useless.
 *
 * Prereqs:
 *   - data/earth/srtm/N39W106.htile present (generated upstream by
 *     horizon_world convert-hgt --in N39W106.hgt --out N39W106.htile).
 *
 * Notes:
 *   - SKIPs cleanly if the fixture is absent.
 *   - Run manually:
 *       ./build/native-linux-debug/bin/dtests/SimEnvironmentTerrain_Dev \
 *           --gtest_filter="HorizonRealSrtm*"
 */

#include "src/sim/environment/terrain/inc/HtileTile.hpp"
#include "src/sim/environment/terrain/inc/earth/SrtmTerrainModel.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <iomanip>
#include <iostream>

using sim::environment::terrain::HtileTile;
using sim::environment::terrain::isSuccess;
using sim::environment::terrain::Status;
using sim::environment::terrain::earth::SrtmTerrainModel;

namespace {

constexpr const char* K_TILE_PATH = "src/sim/environment/terrain/data/earth/srtm/N39W106.htile";

constexpr double K_DEG_PER_RAD = 57.295779513082320876798154814105;

/// Tolerance vs. published USGS values: SRTM1 documented +-16 m absolute,
/// plus a few m of bilinear-quantization slack.
constexpr double K_TOLERANCE_M = 50.0;

/// Known landmark within the tile.
struct Landmark {
  const char* name;
  double lat_deg;
  double lon_deg;
  double expected_elev_m;
};

constexpr Landmark K_LANDMARKS[] = {
    // Mt. Blue Sky (formerly Mt. Evans) summit -- USGS 14,265 ft.
    {"Mt. Blue Sky", 39.5883, -105.6438, 4348.0},
    // Grays Peak -- USGS 14,278 ft. Highest point on the Continental Divide.
    {"Grays Peak", 39.6336, -105.8177, 4351.0},
    // Torreys Peak -- USGS 14,267 ft.
    {"Torreys Peak", 39.6427, -105.8211, 4349.0},
    // Berthoud Pass -- USGS 11,307 ft (US-40 highway pass).
    {"Berthoud Pass", 39.7989, -105.7780, 3447.0},
    // Loveland Pass -- USGS 11,990 ft (US-6 highway pass over Continental Divide).
    {"Loveland Pass", 39.6634, -105.8783, 3654.0},
};

} // namespace

/* ----------------------------- Tests ----------------------------- */

TEST(HorizonRealSrtm, TileLoadsAndHasExpectedHeader) {
  HtileTile t;
  if (!isSuccess(t.load(K_TILE_PATH))) {
    GTEST_SKIP() << "Real SRTM fixture not present: " << K_TILE_PATH
                 << "  (generate via horizon_world convert-hgt N39W106.hgt)";
  }
  EXPECT_EQ(t.header().dim_lat, 3601u);
  EXPECT_EQ(t.header().dim_lon, 3601u);
  EXPECT_DOUBLE_EQ(t.header().lat_min_deg, 39.0);
  EXPECT_DOUBLE_EQ(t.header().lat_max_deg, 40.0);
  EXPECT_DOUBLE_EQ(t.header().lon_min_deg, -106.0);
  EXPECT_DOUBLE_EQ(t.header().lon_max_deg, -105.0);
  EXPECT_STREQ(t.header().body, "earth");
  EXPECT_STREQ(t.header().ref_surface, "egm96");
}

TEST(HorizonRealSrtm, ElevationsMatchUsgsAtKnownLandmarks) {
  HtileTile t;
  if (!isSuccess(t.load(K_TILE_PATH))) {
    GTEST_SKIP() << "Real SRTM fixture not present";
  }

  std::cout << "\n=== Real SRTM ground-truth check (tolerance +-" << K_TOLERANCE_M << " m) ===\n";
  std::cout << std::fixed << std::setprecision(2);

  for (const auto& lm : K_LANDMARKS) {
    double H = 0.0;
    const double LAT = lm.lat_deg / K_DEG_PER_RAD;
    const double LON = lm.lon_deg / K_DEG_PER_RAD;
    ASSERT_EQ(t.elevationAt(LAT, LON, H), Status::SUCCESS) << lm.name << " query failed";
    const double DELTA = H - lm.expected_elev_m;
    std::cout << "  " << std::setw(15) << std::left << lm.name << "  expected " << std::setw(8)
              << std::right << lm.expected_elev_m << "  got " << std::setw(8) << H << "  delta "
              << std::showpos << std::setw(7) << DELTA << std::noshowpos << "\n";
    EXPECT_NEAR(H, lm.expected_elev_m, K_TOLERANCE_M) << "elevation mismatch at " << lm.name;
  }
}

TEST(HorizonRealSrtm, EarthWrapperValidatesAndReadsSamePoints) {
  // Same query, but through the SrtmTerrainModel wrapper. Validates the
  // wrapper accepts our converted real Earth tile (Earth-class metadata
  // check passes) AND returns identical elevations.
  SrtmTerrainModel earth;
  if (!isSuccess(earth.loadEarth(K_TILE_PATH))) {
    GTEST_SKIP() << "Real SRTM fixture not present or wrapper rejected it";
  }
  HtileTile generic;
  ASSERT_EQ(generic.load(K_TILE_PATH), Status::SUCCESS);

  for (const auto& lm : K_LANDMARKS) {
    const double LAT = lm.lat_deg / K_DEG_PER_RAD;
    const double LON = lm.lon_deg / K_DEG_PER_RAD;
    double H_WRAP = 0.0;
    double H_GENERIC = 0.0;
    ASSERT_EQ(earth.elevationAt(LAT, LON, H_WRAP), Status::SUCCESS);
    ASSERT_EQ(generic.elevationAt(LAT, LON, H_GENERIC), Status::SUCCESS);
    EXPECT_DOUBLE_EQ(H_WRAP, H_GENERIC) << "wrapper diverges from generic at " << lm.name;
  }
}

TEST(HorizonRealSrtm, OutOfTileLatLonRejected) {
  HtileTile t;
  if (!isSuccess(t.load(K_TILE_PATH))) {
    GTEST_SKIP() << "Real SRTM fixture not present";
  }
  // Just outside the tile bounds (lat=41 -> north of N40, lon=-104 -> east of W105).
  double H = 0.0;
  EXPECT_EQ(t.elevationAt(41.0 / K_DEG_PER_RAD, -105.5 / K_DEG_PER_RAD, H),
            Status::WARN_OUTSIDE_COVERAGE);
  EXPECT_EQ(t.elevationAt(39.5 / K_DEG_PER_RAD, -104.0 / K_DEG_PER_RAD, H),
            Status::WARN_OUTSIDE_COVERAGE);
}
