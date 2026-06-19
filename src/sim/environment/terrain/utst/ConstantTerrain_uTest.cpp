/**
 * @file ConstantTerrain_uTest.cpp
 * @brief Tests for the constant-elevation terrain model.
 */

#include "src/sim/environment/terrain/inc/ConstantTerrain.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::environment::terrain::ConstantTerrain;
using sim::environment::terrain::isSuccess;
using sim::environment::terrain::Status;

namespace {

/* ----------------------------- Default Construction ----------------------------- */

TEST(ConstantTerrain, DefaultIsZero) {
  ConstantTerrain t;
  EXPECT_DOUBLE_EQ(t.elevation(), 0.0);
  double H = -1.0;
  ASSERT_EQ(t.elevationAt(0.5, 1.0, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 0.0);
}

TEST(ConstantTerrain, ConstructorSetsValue) {
  ConstantTerrain t(123.4);
  EXPECT_DOUBLE_EQ(t.elevation(), 123.4);
  double H = 0.0;
  ASSERT_EQ(t.elevationAt(-0.3, 2.5, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 123.4);
}

TEST(ConstantTerrain, SetUpdatesValue) {
  ConstantTerrain t;
  t.setElevation(-50.0);
  double H = 0.0;
  ASSERT_EQ(t.elevationAt(0.0, 0.0, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, -50.0);
}

/* ----------------------------- API Tests ----------------------------- */

TEST(ConstantTerrain, EcefMatchesGeodetic) {
  ConstantTerrain t(42.0);
  const double EC[3] = {1e6, 2e6, 3e6};
  double H = 0.0;
  ASSERT_EQ(t.elevationAtEcef(EC, H), Status::SUCCESS);
  EXPECT_DOUBLE_EQ(H, 42.0);
}

TEST(ConstantTerrain, GlobalCoverage) {
  ConstantTerrain t(0.0);
  EXPECT_NEAR(t.minLatRad(), -M_PI / 2, 1e-12);
  EXPECT_NEAR(t.maxLatRad(), M_PI / 2, 1e-12);
  EXPECT_NEAR(t.minLonRad(), -M_PI, 1e-12);
  EXPECT_NEAR(t.maxLonRad(), M_PI, 1e-12);
  EXPECT_TRUE(t.isInCoverage(0.0, 0.0));
  EXPECT_TRUE(t.isInCoverage(M_PI / 2, M_PI));
}

TEST(ConstantTerrain, ResolutionIsZeroForAnalytic) {
  ConstantTerrain t;
  EXPECT_DOUBLE_EQ(t.resolutionMeters(), 0.0);
}

} // namespace
