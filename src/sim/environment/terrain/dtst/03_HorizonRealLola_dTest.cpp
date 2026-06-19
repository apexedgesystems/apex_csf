/**
 * @file 03_HorizonRealLola_dTest.cpp
 * @brief Ground-truth integration test: convert real LOLA `ldem_16.img`
 *        through horizon's CLI -> htile, load via apex's HtileTile +
 *        LolaTerrainModel, verify elevations at well-known lunar features.
 *
 * Why this matters:
 *   The Earth dtest validates the SRTM path against USGS landmarks; this is
 *   the moon equivalent. The entire LOLA path (PDS IMG+LBL -> htile ->
 *   HtileTile bilinear lookup) needs the same coordinate-convention /
 *   row-order / scaling validation against external truth.
 *
 * Tile coverage:  whole moon, 5760 x 2880 at 16 ppd (~1.85 km/sample
 *                 at the equator). LON convention is 0-360 east (LOLA
 *                 standard, NOT -180..180).
 *
 * Tolerance reasoning:
 *   - LOLA-derived elevations have ~10 m absolute accuracy, but ldem_16
 *     is the GLOBAL coarse product at 1.85 km/sample; bilinear lookup
 *     near broad features can differ from "the literature value" by
 *     several hundred meters (the literature is often a more accurate
 *     LOLA-derived point, sampled at higher resolution).
 *   - We use range-based assertions for the broad topographic features
 *     (mare floor must be negative, SPA basin must be deep, far-side
 *     highlands must be elevated). That's what the coarse product can
 *     reliably verify.
 *
 * Prereqs:
 *   - data/moon/lola/ldem_16.htile present (generated upstream by
 *     horizon_world convert-pds --in ldem_16.img --out ldem_16.htile).
 *
 * Notes:
 *   - SKIPs cleanly if the fixture is absent.
 *   - Run manually:
 *       ./build/native-linux-debug/bin/dtests/SimEnvironmentTerrain_Dev \
 *           --gtest_filter="HorizonRealLola*"
 */

#include "src/sim/environment/terrain/inc/HtileTile.hpp"
#include "src/sim/environment/terrain/inc/moon/LolaTerrainModel.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <iomanip>
#include <iostream>

using sim::environment::terrain::HtileTile;
using sim::environment::terrain::isSuccess;
using sim::environment::terrain::Status;
using sim::environment::terrain::moon::LolaTerrainModel;

namespace {

constexpr const char* K_TILE_PATH = "src/sim/environment/terrain/data/moon/lola/ldem_16.htile";

constexpr double K_DEG_PER_RAD = 57.295779513082320876798154814105;

/// Lunar reference radius (meters above which positive elevations sit).
constexpr double K_LUNAR_R_REF_M = 1737400.0;

} // namespace

/* ----------------------------- Tests ----------------------------- */

TEST(HorizonRealLola, TileLoadsAndHasExpectedHeader) {
  HtileTile t;
  if (!isSuccess(t.load(K_TILE_PATH))) {
    GTEST_SKIP() << "Real LOLA fixture not present: " << K_TILE_PATH
                 << "  (generate via horizon_world convert-pds ldem_16.img)";
  }
  // ldem_16 is the global LOLA 16 ppd product.
  EXPECT_EQ(t.header().dim_lat, 2880u); // -90..90 deg / (1/16 ppd)^-1 = 2880 rows
  EXPECT_EQ(t.header().dim_lon, 5760u); // 0..360 deg / same = 5760 cols
  EXPECT_DOUBLE_EQ(t.header().lat_min_deg, -90.0);
  EXPECT_DOUBLE_EQ(t.header().lat_max_deg, 90.0);
  EXPECT_DOUBLE_EQ(t.header().lon_min_deg, 0.0);
  EXPECT_DOUBLE_EQ(t.header().lon_max_deg, 360.0);
  EXPECT_DOUBLE_EQ(t.header().ref_radius_m, K_LUNAR_R_REF_M);
  EXPECT_STREQ(t.header().body, "moon");
  EXPECT_STREQ(t.header().ref_surface, "sphere");
  // LOLA SCALING_FACTOR is 0.5 m/DN -- our converter must propagate it.
  EXPECT_DOUBLE_EQ(t.header().scale_m_per_dn, 0.5);
}

TEST(HorizonRealLola, MareFloorIsNegativeAtApolloSites) {
  HtileTile t;
  if (!isSuccess(t.load(K_TILE_PATH))) {
    GTEST_SKIP() << "Real LOLA fixture not present";
  }

  // Apollo landing sites in mare basins -- elevation should be NEGATIVE
  // (below the 1737.4 km reference sphere) by at least ~1000 m, since
  // Mare basins are documented to sit -1500 m to -3500 m below ref.
  // LOLA uses 0-360 lon (east positive); negative-east sites convert via
  // (lon + 360) mod 360.
  struct Site {
    const char* name;
    double lat_deg;
    double lon_deg_east; // 0-360 convention
    // Approximate published mare-floor elevation, m.
    double expected_low;
    double expected_high;
  };
  const Site SITES[] = {
      // Apollo 11 (Sea of Tranquility) -- typical mare floor.
      {"Apollo 11", 0.6741, 23.4733, -3000.0, -500.0},
      // Apollo 12 (Oceanus Procellarum).
      {"Apollo 12", -3.0124, 360.0 - 23.4216, -3000.0, -500.0},
      // Apollo 14 (Fra Mauro hills, near mare).
      {"Apollo 14", -3.6453, 360.0 - 17.4714, -3000.0, -500.0},
      // Apollo 15 (Hadley-Apennine, mare edge).
      {"Apollo 15", 26.1322, 3.6336, -3000.0, -500.0},
      // Apollo 17 (Taurus-Littrow valley) -- deeper since it's a valley
      // between the Taurus mountains (~-2.6 km observed at 16 ppd).
      {"Apollo 17", 20.1908, 30.7717, -3500.0, -500.0},
  };

  std::cout << "\n=== Real LOLA ground-truth: Apollo landing sites ===\n";
  std::cout << std::fixed << std::setprecision(1);
  for (const auto& s : SITES) {
    double H = 0.0;
    const double LAT = s.lat_deg / K_DEG_PER_RAD;
    const double LON = s.lon_deg_east / K_DEG_PER_RAD;
    ASSERT_EQ(t.elevationAt(LAT, LON, H), Status::SUCCESS) << s.name << " query failed";
    std::cout << "  " << std::setw(10) << std::left << s.name << "  lat " << std::setw(7)
              << std::right << s.lat_deg << "  lon_east " << std::setw(8) << s.lon_deg_east
              << "  H " << std::setw(8) << H << "  expected [" << s.expected_low << ", "
              << s.expected_high << "]\n";
    EXPECT_GE(H, s.expected_low) << s.name << " too low";
    EXPECT_LE(H, s.expected_high) << s.name << " too high (mare site should be below ref)";
  }
}

TEST(HorizonRealLola, SpaBasinIsDeepest) {
  HtileTile t;
  if (!isSuccess(t.load(K_TILE_PATH))) {
    GTEST_SKIP() << "Real LOLA fixture not present";
  }

  // South Pole-Aitken Basin is the deepest known lunar feature
  // (rim-to-floor ~13 km; absolute floor depths vary -3 to -9 km below
  // the reference sphere depending on which point is sampled at full
  // LOLA resolution). At 16 ppd, the bilinear-smoothed value will be
  // shallower than the literature deepest point. We verify across a
  // few inner-basin points that SPA reads SIGNIFICANTLY deeper than
  // typical mare basins (at minimum < -3000 m, ideally < -4000 m).
  const double POINTS[][2] = {
      {-53.0, 169.0},
      {-55.0, 200.0},
      {-50.0, 200.0},
      {-45.0, 200.0},
  };
  std::cout << "\n=== Real LOLA ground-truth: SPA basin region ===\n";
  std::cout << std::fixed << std::setprecision(1);
  double minH = 1e9;
  for (const auto& p : POINTS) {
    double H = 0.0;
    ASSERT_EQ(t.elevationAt(p[0] / K_DEG_PER_RAD, p[1] / K_DEG_PER_RAD, H), Status::SUCCESS);
    std::cout << "  (" << std::setw(6) << p[0] << ", " << std::setw(6) << p[1]
              << ")  H = " << std::setw(8) << H << " m\n";
    if (H < minH)
      minH = H;
  }
  // Deepest sampled point should be markedly below typical mare floors.
  EXPECT_LT(minH, -3000.0) << "SPA basin region should sample at least one point below -3000 m";
}

TEST(HorizonRealLola, FarsideHighlandsArePositive) {
  HtileTile t;
  if (!isSuccess(t.load(K_TILE_PATH))) {
    GTEST_SKIP() << "Real LOLA fixture not present";
  }

  // Lunar far-side highlands sit above the reference sphere on average.
  // Pick a few points in the central far side away from SPA.
  const double POINTS[][2] = {
      {0.0, 180.0},   // far-side equator
      {30.0, 200.0},  // northeastern far side
      {-20.0, 210.0}, // southeastern far side (away from SPA)
  };
  std::cout << "\n=== Real LOLA ground-truth: far-side highlands ===\n";
  std::cout << std::fixed << std::setprecision(1);
  int positive_count = 0;
  for (const auto& p : POINTS) {
    double H = 0.0;
    ASSERT_EQ(t.elevationAt(p[0] / K_DEG_PER_RAD, p[1] / K_DEG_PER_RAD, H), Status::SUCCESS);
    std::cout << "  (" << std::setw(6) << p[0] << ", " << std::setw(6) << p[1]
              << ")  H = " << std::setw(8) << H << " m\n";
    if (H > 0.0)
      ++positive_count;
  }
  // At least 2/3 of these points should sit above the reference sphere.
  EXPECT_GE(positive_count, 2)
      << "Far-side highlands should sit above the lunar reference sphere on average";
}

TEST(HorizonRealLola, MoonWrapperValidatesAndReadsSamePoints) {
  // Same query through the LolaTerrainModel wrapper -- validates Moon
  // metadata accept + identical elevation results.
  LolaTerrainModel moon;
  if (!isSuccess(moon.loadMoon(K_TILE_PATH))) {
    GTEST_SKIP() << "Real LOLA fixture not present or wrapper rejected it";
  }
  HtileTile generic;
  ASSERT_EQ(generic.load(K_TILE_PATH), Status::SUCCESS);

  // Apollo 11 site, both readers should agree exactly.
  const double LAT = 0.6741 / K_DEG_PER_RAD;
  const double LON = 23.4733 / K_DEG_PER_RAD;
  double H_WRAP = 0.0;
  double H_GENERIC = 0.0;
  ASSERT_EQ(moon.elevationAt(LAT, LON, H_WRAP), Status::SUCCESS);
  ASSERT_EQ(generic.elevationAt(LAT, LON, H_GENERIC), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H_WRAP, H_GENERIC);
}

TEST(HorizonRealLola, OutOfTileLatRejected) {
  HtileTile t;
  if (!isSuccess(t.load(K_TILE_PATH))) {
    GTEST_SKIP() << "Real LOLA fixture not present";
  }
  // Latitude outside the tile's [-90, 90] window is genuinely uncovered.
  double H = 0.0;
  EXPECT_EQ(t.elevationAt(95.0 / K_DEG_PER_RAD, 0.0, H), Status::WARN_OUTSIDE_COVERAGE);
  EXPECT_EQ(t.elevationAt(-95.0 / K_DEG_PER_RAD, 0.0, H), Status::WARN_OUTSIDE_COVERAGE);
}

TEST(HorizonRealLola, WraparoundLongitudeAccepted) {
  HtileTile t;
  if (!isSuccess(t.load(K_TILE_PATH))) {
    GTEST_SKIP() << "Real LOLA fixture not present";
  }
  // B4: this 0..360 tile must accept a [-180, 180] longitude (the form
  // elevationAtEcef's atan2 produces). -10 deg normalizes to 350 deg and
  // 370 deg normalizes to 10 deg -- both inside coverage and equal to the
  // direct 0..360 query at the same physical longitude.
  double H_NEG = 0.0;
  double H_350 = 0.0;
  ASSERT_EQ(t.elevationAt(0.0, -10.0 / K_DEG_PER_RAD, H_NEG), Status::SUCCESS);
  ASSERT_EQ(t.elevationAt(0.0, 350.0 / K_DEG_PER_RAD, H_350), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H_NEG, H_350);

  double H_370 = 0.0;
  double H_10 = 0.0;
  ASSERT_EQ(t.elevationAt(0.0, 370.0 / K_DEG_PER_RAD, H_370), Status::SUCCESS);
  ASSERT_EQ(t.elevationAt(0.0, 10.0 / K_DEG_PER_RAD, H_10), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H_370, H_10);
}
