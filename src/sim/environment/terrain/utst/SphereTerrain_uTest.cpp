/**
 * @file SphereTerrain_uTest.cpp
 * @brief Tests for the spherical-body terrain model.
 */

#include "src/sim/environment/terrain/inc/SphereTerrain.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::environment::terrain::SphereTerrain;
using sim::environment::terrain::Status;

namespace {

constexpr double K_LUNAR_R_M = 1737400.0;

/* ----------------------------- Construction ----------------------------- */

TEST(SphereTerrain, ConstructorSetsRadius) {
  SphereTerrain t(K_LUNAR_R_M);
  EXPECT_DOUBLE_EQ(t.radius(), K_LUNAR_R_M);
}

/* ----------------------------- Geodetic Query ----------------------------- */

TEST(SphereTerrain, GeodeticIsZeroEverywhere) {
  SphereTerrain t(K_LUNAR_R_M);
  double H = 999.0;
  ASSERT_EQ(t.elevationAt(0.0, 0.0, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 0.0);
  ASSERT_EQ(t.elevationAt(M_PI / 4, M_PI / 3, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 0.0);
}

/* ----------------------------- ECEF Query ----------------------------- */

TEST(SphereTerrain, EcefAtSphereSurfaceReturnsZero) {
  SphereTerrain t(K_LUNAR_R_M);
  // Point exactly on +x axis at the sphere's radius.
  const double EC[3] = {K_LUNAR_R_M, 0.0, 0.0};
  double H = -1.0;
  ASSERT_EQ(t.elevationAtEcef(EC, H), Status::SUCCESS);
  EXPECT_NEAR(H, 0.0, 1e-9);
}

TEST(SphereTerrain, EcefAboveSurfacePositive) {
  SphereTerrain t(K_LUNAR_R_M);
  // 100 km above the surface.
  const double EC[3] = {K_LUNAR_R_M + 1.0e5, 0.0, 0.0};
  double H = 0.0;
  ASSERT_EQ(t.elevationAtEcef(EC, H), Status::SUCCESS);
  EXPECT_NEAR(H, 1.0e5, 1e-6);
}

TEST(SphereTerrain, EcefBelowSurfaceNegative) {
  SphereTerrain t(K_LUNAR_R_M);
  // 1 km below the surface.
  const double EC[3] = {K_LUNAR_R_M - 1000.0, 0.0, 0.0};
  double H = 0.0;
  ASSERT_EQ(t.elevationAtEcef(EC, H), Status::SUCCESS);
  EXPECT_NEAR(H, -1000.0, 1e-6);
}

TEST(SphereTerrain, EcefNullptrRejected) {
  SphereTerrain t(K_LUNAR_R_M);
  double H = 0.0;
  EXPECT_EQ(t.elevationAtEcef(nullptr, H), Status::ERROR_PARAM_BUFFER_NULL);
}

/* ----------------------------- Coverage ----------------------------- */

TEST(SphereTerrain, GlobalCoverage) {
  SphereTerrain t(K_LUNAR_R_M);
  EXPECT_TRUE(t.isInCoverage(M_PI / 2, M_PI));
  EXPECT_TRUE(t.isInCoverage(-M_PI / 2, -M_PI));
  EXPECT_DOUBLE_EQ(t.resolutionMeters(), 0.0);
}

} // namespace
