/**
 * @file Celestial_uTest.cpp
 * @brief Consistency tests for the canonical celestial constants.
 *
 * Constants cannot be "tested" against themselves, so the suite checks the
 * relations that must hold between them (derived-value consistency), the
 * agreement with the sim-side copies they are canon for (guarding the
 * adoption-branch migration), and the conversion helpers.
 */

#include "src/utilities/math/celestial/inc/Angles.hpp"
#include "src/utilities/math/celestial/inc/EarthConstants.hpp"
#include "src/utilities/math/celestial/inc/MoonConstants.hpp"

// Sim-side copies this leaf is canon for: equality locks the migration.
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"
#include "src/sim/environment/gravity/inc/moon/LunarConstants.hpp"
#include "src/sim/environment/terrain/inc/earth/Wgs84TerrainConstants.hpp"

#include <cmath>
#include <gtest/gtest.h>

namespace cel = apex::math::celestial;

/* ------------------------- Internal consistency -------------------------- */

/** @test Derived ellipsoid values agree with the defining parameters. */
TEST(CelestialTest, Wgs84DerivedConsistency) {
  EXPECT_NEAR(cel::earth::B, cel::earth::A * (1.0 - cel::earth::F), 5e-5);
  EXPECT_NEAR(cel::earth::E2, cel::earth::F * (2.0 - cel::earth::F), 1e-14);
  EXPECT_NEAR(cel::earth::EP2, cel::earth::E2 / (1.0 - cel::earth::E2), 1e-13);
}

/** @test Earth rotation rate matches the sidereal day it implies. */
TEST(CelestialTest, EarthOmegaImpliesSiderealDay) {
  const double SIDEREAL_DAY_S = cel::TWO_PI / cel::earth::OMEGA;
  EXPECT_NEAR(SIDEREAL_DAY_S, 86164.1, 0.5); // ~23h 56m 4.1s
}

/** @test Tidal lock: moon OMEGA and T_SIDEREAL describe the same rotation. */
TEST(CelestialTest, MoonOmegaMatchesSiderealPeriod) {
  EXPECT_NEAR(cel::moon::OMEGA, cel::TWO_PI / cel::moon::T_SIDEREAL, 1e-10);
}

/** @test Angle helpers round-trip and hit the known anchors. */
TEST(CelestialTest, AngleConversions) {
  EXPECT_NEAR(cel::degToRad(180.0), cel::PI, 1e-15);
  EXPECT_NEAR(cel::radToDeg(cel::HALF_PI), 90.0, 1e-12);
  EXPECT_NEAR(cel::degToRad(cel::radToDeg(1.234567)), 1.234567, 1e-15);
  EXPECT_NEAR(cel::DEG_TO_RAD, 0.017453292519943295, 1e-18);
}

/* --------------------- Canon vs the sim-side copies ----------------------- */

/** @test The canon equals the gravity/terrain copies it will replace. */
TEST(CelestialTest, CanonMatchesSimCopies) {
  namespace gw = sim::environment::gravity::wgs84;
  namespace tw = sim::environment::terrain::earth::wgs84;
  namespace gl = sim::environment::gravity::lunar;

  EXPECT_EQ(cel::earth::A, gw::A);
  EXPECT_EQ(cel::earth::A, tw::R_EQ_M);
  EXPECT_EQ(cel::earth::B, gw::B);
  EXPECT_EQ(cel::earth::B, tw::R_POL_M);
  EXPECT_EQ(cel::earth::F, gw::F);
  EXPECT_EQ(cel::earth::E2, gw::E2);
  EXPECT_EQ(cel::earth::EP2, gw::EP2);
  EXPECT_EQ(cel::earth::OMEGA, gw::OMEGA);

  EXPECT_EQ(cel::moon::R_MEAN, gl::R_MEAN);
  EXPECT_EQ(cel::moon::T_SIDEREAL, gl::T_ORBIT);
  EXPECT_EQ(cel::moon::OMEGA, gl::OMEGA);
}
