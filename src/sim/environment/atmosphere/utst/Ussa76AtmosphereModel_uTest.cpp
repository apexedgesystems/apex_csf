/**
 * @file Ussa76AtmosphereModel_uTest.cpp
 * @brief Tests for the Earth USSA76 wrapper.
 *
 * The wrapper bakes the 7-layer USSA76 table + dry-air thermo constants into
 * a default-constructed, ready-to-query model (no load/init needed). These
 * tests verify it is pre-loaded, carries the canonical layer boundaries, and
 * reproduces the published sea-level reference state.
 */

#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"
#include "src/sim/environment/atmosphere/inc/earth/Ussa76AtmosphereModel.hpp"
#include "src/sim/environment/atmosphere/inc/earth/Ussa76Constants.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::environment::atmosphere::AtmosphereState;
using sim::environment::atmosphere::isSuccess;
using sim::environment::atmosphere::earth::Ussa76AtmosphereModel;
namespace c = sim::environment::atmosphere::earth;

namespace {

TEST(Ussa76AtmosphereModel, DefaultConstructedIsReady) {
  Ussa76AtmosphereModel a;
  EXPECT_TRUE(a.isLoaded());
  EXPECT_EQ(a.numLayers(), c::NUM_LAYERS);
  EXPECT_FALSE(a.isVacuum());
  EXPECT_DOUBLE_EQ(a.gasConstant(), c::R_SPECIFIC);
  EXPECT_DOUBLE_EQ(a.gamma(), c::GAMMA);
  EXPECT_DOUBLE_EQ(a.surfaceGravity(), c::G0);
}

TEST(Ussa76AtmosphereModel, LayerBoundariesAreCanonical) {
  Ussa76AtmosphereModel a;
  ASSERT_EQ(a.numLayers(), 7u);
  EXPECT_DOUBLE_EQ(a.layer(0).base_alt_m, 0.0);
  EXPECT_DOUBLE_EQ(a.layer(1).base_alt_m, 11000.0);
  EXPECT_DOUBLE_EQ(a.layer(6).base_alt_m, 71000.0);
  // Validity reaches the documented top (71 km base + 5 km extrapolation).
  EXPECT_DOUBLE_EQ(a.minAltitudeM(), 0.0);
  EXPECT_DOUBLE_EQ(a.maxAltitudeM(), 76000.0);
}

TEST(Ussa76AtmosphereModel, SeaLevelMatchesPublishedReference) {
  Ussa76AtmosphereModel a;
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(0.0, 0.0, 0.0, s)));
  EXPECT_NEAR(s.T, 288.15, 0.01);
  EXPECT_NEAR(s.P, 101325.0, 1.0);
  EXPECT_NEAR(s.rho, 1.225, 0.001);
  EXPECT_NEAR(s.a, 340.294, 0.01);
}

TEST(Ussa76AtmosphereModel, ConstantsMatchSharedTable) {
  // The wrapper must consume the same constants the header advertises, so a
  // file-backed LayeredAtmosphere and the wrapper agree by construction.
  Ussa76AtmosphereModel a;
  for (std::size_t i = 0; i < a.numLayers(); ++i) {
    EXPECT_DOUBLE_EQ(a.layer(i).base_alt_m, c::LAYERS[i].base_alt_m) << "layer " << i;
    EXPECT_DOUBLE_EQ(a.layer(i).base_T_K, c::LAYERS[i].base_T_K) << "layer " << i;
    EXPECT_DOUBLE_EQ(a.layer(i).base_P_Pa, c::LAYERS[i].base_P_Pa) << "layer " << i;
    EXPECT_DOUBLE_EQ(a.layer(i).lapse_K_per_m, c::LAYERS[i].lapse_K_per_m) << "layer " << i;
  }
}

} // namespace
