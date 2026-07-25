/**
 * @file Celestial_uTest.cpp
 * @brief Consistency tests for the canonical celestial constants.
 *
 * Constants cannot be "tested" against themselves, so the suite checks the
 * relations that must hold between them (derived-value consistency), the
 * agreement with the sim-side copies they are canon for (guarding the
 * adoption-branch migration), and the conversion helpers.
 */

#include "src/utilities/math/vecmat/inc/Angles.hpp"
#include "src/utilities/math/celestial/inc/EarthConstants.hpp"
#include "src/utilities/math/celestial/inc/MoonConstants.hpp"

// Sim-side copies this leaf is canon for: equality locks the migration.
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"
#include "src/sim/environment/gravity/inc/moon/LunarConstants.hpp"
#include "src/sim/environment/terrain/inc/earth/Wgs84TerrainConstants.hpp"
#include "src/sim/environment/terrain/inc/moon/LunarTerrainConstants.hpp"
#include "src/sim/environment/atmosphere/inc/earth/Ussa76Constants.hpp"
#include "src/sim/environment/gravity/inc/ConstantGravityModel.hpp"
#include "src/sim/sensors/inc/GPS.hpp"

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
  const double SIDEREAL_DAY_S = apex::math::vecmat::TWO_PI / cel::earth::OMEGA;
  EXPECT_NEAR(SIDEREAL_DAY_S, 86164.1, 0.5); // ~23h 56m 4.1s
}

/** @test Tidal lock: moon OMEGA and T_SIDEREAL describe the same rotation. */
TEST(CelestialTest, MoonOmegaMatchesSiderealPeriod) {
  EXPECT_NEAR(cel::moon::OMEGA, apex::math::vecmat::TWO_PI / cel::moon::T_SIDEREAL, 1e-10);
}

/** @test Angle helpers round-trip and hit the known anchors. */
TEST(CelestialTest, AngleConversions) {
  EXPECT_NEAR(apex::math::vecmat::degToRad(180.0), apex::math::vecmat::PI, 1e-15);
  EXPECT_NEAR(apex::math::vecmat::radToDeg(apex::math::vecmat::HALF_PI), 90.0, 1e-12);
  EXPECT_NEAR(apex::math::vecmat::degToRad(apex::math::vecmat::radToDeg(1.234567)), 1.234567,
              1e-15);
  EXPECT_NEAR(apex::math::vecmat::DEG_TO_RAD, 0.017453292519943295, 1e-18);
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
  EXPECT_EQ(cel::moon::GM, gl::GM);
  EXPECT_EQ(cel::moon::J2, sim::environment::gravity::grgm1200a::J2);
  EXPECT_EQ(cel::moon::G_SURFACE, gl::G_SURFACE);
  EXPECT_EQ(cel::moon::C20, sim::environment::gravity::grgm1200a::C20);
  EXPECT_EQ(cel::moon::R_REF, gl::R_REF);
  EXPECT_EQ(cel::moon::R_MEAN, sim::environment::terrain::moon::lunar::R_REF_M);
  EXPECT_EQ(cel::moon::T_SIDEREAL, gl::T_ORBIT);
  EXPECT_EQ(cel::moon::OMEGA, gl::OMEGA);

  EXPECT_EQ(cel::earth::GM, gw::GM);
  EXPECT_EQ(cel::earth::J2, sim::environment::gravity::egm2008::J2);
  EXPECT_EQ(cel::earth::J3, sim::environment::gravity::egm2008::J3);
  EXPECT_EQ(cel::earth::J4, sim::environment::gravity::egm2008::J4);
  EXPECT_EQ(cel::earth::C20, sim::environment::gravity::egm2008::C20);
  EXPECT_EQ(cel::earth::G0, sim::environment::atmosphere::earth::G0);
  EXPECT_EQ(cel::earth::G0, sim::environment::gravity::DEFAULT_G0);
  EXPECT_EQ(cel::earth::A, sim::sensors::GPSParams{}.earth_radius_m);
}

/** @test Each un-normalized Jn is -sqrt(2n+1) times its normalized Cn0:
 *  the two families are one dataset (EGM2008 as distributed), and any
 *  edit that breaks the derivation fails here. */
/** @test The lunar canon is self-consistent: J2 derives from the
 *  distributed C20 and the surface gravity from GM over the mean
 *  radius squared. */
TEST(CelestialTest, MoonCanonIsSelfConsistent) {
  EXPECT_NEAR(cel::moon::J2, -std::sqrt(5.0) * cel::moon::C20, 1e-14 * cel::moon::J2);
  EXPECT_NEAR(cel::moon::G_SURFACE, cel::moon::GM / (cel::moon::R_MEAN * cel::moon::R_MEAN),
              1e-14 * cel::moon::G_SURFACE);
}

TEST(CelestialTest, ZonalFamilyIsOneDataset) {
  EXPECT_NEAR(cel::earth::J2, -std::sqrt(5.0) * cel::earth::C20, 1e-14 * std::abs(cel::earth::J2));
  EXPECT_NEAR(cel::earth::J3, -std::sqrt(7.0) * cel::earth::C30, 1e-14 * std::abs(cel::earth::J3));
  EXPECT_NEAR(cel::earth::J4, -3.0 * cel::earth::C40, 1e-14 * std::abs(cel::earth::J4));
  EXPECT_NEAR(cel::earth::J5, -std::sqrt(11.0) * cel::earth::C50, 1e-14 * std::abs(cel::earth::J5));
  EXPECT_NEAR(cel::earth::J6, -std::sqrt(13.0) * cel::earth::C60, 1e-14 * std::abs(cel::earth::J6));
}
