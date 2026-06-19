/**
 * @file EllipsoidTerrain_uTest.cpp
 * @brief Tests for the oblate-ellipsoid analytic terrain model.
 */

#include "src/sim/environment/terrain/inc/EllipsoidTerrain.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::environment::terrain::EllipsoidTerrain;
using sim::environment::terrain::Status;

namespace {

constexpr double WGS84_A = 6378137.0;
constexpr double WGS84_B = 6356752.3142;

/* ----------------------------- Geodetic Query ----------------------------- */

TEST(EllipsoidTerrain, GeodeticIsZeroEverywhere) {
  EllipsoidTerrain t(WGS84_A, WGS84_B);
  double H = 999.0;
  ASSERT_EQ(t.elevationAt(0.0, 0.0, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 0.0);
  ASSERT_EQ(t.elevationAt(M_PI / 4, -M_PI / 6, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 0.0);
}

/* ----------------------------- ECEF Query ----------------------------- */

TEST(EllipsoidTerrain, EcefAtEquatorIsZero) {
  EllipsoidTerrain t(WGS84_A, WGS84_B);
  // +x equator: should sit on the ellipsoid -> H = 0.
  const double EC[3] = {WGS84_A, 0.0, 0.0};
  double H = 999.0;
  ASSERT_EQ(t.elevationAtEcef(EC, H), Status::SUCCESS);
  EXPECT_NEAR(H, 0.0, 1e-3); // ~mm tolerance for Bowring iteration
}

TEST(EllipsoidTerrain, EcefAtPoleIsZero) {
  EllipsoidTerrain t(WGS84_A, WGS84_B);
  // +z pole: should sit on the ellipsoid -> H = 0.
  const double EC[3] = {0.0, 0.0, WGS84_B};
  double H = 999.0;
  ASSERT_EQ(t.elevationAtEcef(EC, H), Status::SUCCESS);
  EXPECT_NEAR(H, 0.0, 1e-3);
}

TEST(EllipsoidTerrain, EcefAboveEquatorMatchesAltitude) {
  EllipsoidTerrain t(WGS84_A, WGS84_B);
  // 400 km above the equator on +x.
  const double ALT = 400000.0;
  const double EC[3] = {WGS84_A + ALT, 0.0, 0.0};
  double H = 0.0;
  ASSERT_EQ(t.elevationAtEcef(EC, H), Status::SUCCESS);
  EXPECT_NEAR(H, ALT, 1e-3);
}

TEST(EllipsoidTerrain, EcefAbovePoleMatchesAltitude) {
  EllipsoidTerrain t(WGS84_A, WGS84_B);
  const double ALT = 100000.0;
  const double EC[3] = {0.0, 0.0, WGS84_B + ALT};
  double H = 0.0;
  ASSERT_EQ(t.elevationAtEcef(EC, H), Status::SUCCESS);
  EXPECT_NEAR(H, ALT, 1e-3);
}

TEST(EllipsoidTerrain, NullptrRejected) {
  EllipsoidTerrain t(WGS84_A, WGS84_B);
  double H = 0.0;
  EXPECT_EQ(t.elevationAtEcef(nullptr, H), Status::ERROR_PARAM_BUFFER_NULL);
}

TEST(EllipsoidTerrain, RadiiAccessors) {
  EllipsoidTerrain t(WGS84_A, WGS84_B);
  EXPECT_DOUBLE_EQ(t.equatorialRadius(), WGS84_A);
  EXPECT_DOUBLE_EQ(t.polarRadius(), WGS84_B);
}

} // namespace
