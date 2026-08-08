/**
 * @file WorldQueryProbe_uTest.cpp
 * @brief Unit tests for the WorldQueryProbe demo component.
 *
 * Covers:
 *   - Failure accounting when unattached, body-not-ready, or zero points.
 *   - Survey rotation: telemetry tracks the configured points and the
 *     round-robin cursor wraps.
 *   - Telemetry snapshot correctness against an analytic Earth body.
 *   - Vacuum-body semantics: zero density is a successful query.
 *
 * Bodies use purely analytic fidelities (no data files); probeTick is
 * driven directly — executive/TPRM lifecycle is exercised by the demo
 * executive's own tests.
 */

#include "demos/apex_horizon_demo/world_query_probe/inc/WorldQueryProbe.hpp"

#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/factory/inc/Body.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFidelity.hpp"

#include <gtest/gtest.h>

#include <cstring>

using appsim::world_query_probe::WorldQueryProbe;
using appsim::world_query_probe::WorldQueryProbeTunables;
using sim::environment::AtmosphereFidelity;
using sim::environment::Body;
using sim::environment::GravityFidelity;
using sim::environment::TerrainFidelity;
using sim::environment::celestial_body::CelestialBody;
using sim::environment::celestial_body::CelestialBodyTunables;

namespace {

CelestialBodyTunables analyticEarth() {
  CelestialBodyTunables t{};
  t.body = Body::EARTH;
  t.gravity_fidelity = GravityFidelity::J2;
  t.terrain_fidelity = TerrainFidelity::ELLIPSOID;
  t.atmosphere_fidelity = AtmosphereFidelity::EXPONENTIAL;
  return t;
}

CelestialBodyTunables analyticMoon() {
  CelestialBodyTunables t{};
  t.body = Body::MOON;
  t.gravity_fidelity = GravityFidelity::J2;
  t.terrain_fidelity = TerrainFidelity::SPHERE;
  t.atmosphere_fidelity = AtmosphereFidelity::CONSTANT; // vacuum
  return t;
}

void configureSurvey(WorldQueryProbe& probe, std::uint32_t n) {
  auto& p = probe.tunables().get();
  p.num_points = n;
  const double LATS[3] = {39.5, -10.0, 71.0};
  const double LONS[3] = {-105.5, 20.0, -8.0};
  for (std::uint32_t i = 0; i < n && i < 3u; ++i) {
    p.lat_deg[i] = LATS[i];
    p.lon_deg[i] = LONS[i];
  }
  p.atmosphere_alt_m = 1000.0;
  std::strncpy(p.body_label, "test_body", sizeof(p.body_label) - 1);
}

} // namespace

/* ----------------------------- Failure accounting ----------------------------- */

/** @test Unattached probe fails the query and counts it. */
TEST(WorldQueryProbe, UnattachedCountsFailures) {
  WorldQueryProbe probe;
  configureSurvey(probe, 2u);

  EXPECT_EQ(probe.probeTick(), 0u);
  EXPECT_EQ(probe.probeTick(), 0u);

  EXPECT_EQ(probe.probeState().consecutive_failures, 2u);
  EXPECT_EQ(probe.telemetry().last_query_succeeded, 0u);
}

/** @test Attached but not-ready body is a failure, not a crash. */
TEST(WorldQueryProbe, NotReadyBodyCountsFailures) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  // No init(): body stays not-ready.

  WorldQueryProbe probe;
  probe.setBody(&earth);
  configureSurvey(probe, 1u);

  EXPECT_EQ(probe.probeTick(), 0u);
  EXPECT_EQ(probe.probeState().consecutive_failures, 1u);
}

/** @test Zero configured points is a failure path, not a division. */
TEST(WorldQueryProbe, ZeroPointsCountsFailures) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  WorldQueryProbe probe;
  probe.setBody(&earth);
  configureSurvey(probe, 0u);

  EXPECT_EQ(probe.probeTick(), 0u);
  EXPECT_EQ(probe.probeState().consecutive_failures, 1u);
}

/* ----------------------------- Survey rotation + snapshot ----------------------------- */

/** @test Telemetry tracks the configured points; the cursor wraps. */
TEST(WorldQueryProbe, SurveyRotationWrapsAndSnapshots) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);
  ASSERT_TRUE(earth.isReady());

  WorldQueryProbe probe;
  probe.setBody(&earth);
  configureSurvey(probe, 2u);

  EXPECT_EQ(probe.probeTick(), 0u);
  EXPECT_EQ(probe.telemetry().last_point_index, 0u);
  EXPECT_DOUBLE_EQ(probe.telemetry().last_lat_deg, 39.5);
  EXPECT_DOUBLE_EQ(probe.telemetry().last_lon_deg, -105.5);
  EXPECT_EQ(probe.telemetry().last_query_succeeded, 1u);
  EXPECT_GT(probe.telemetry().last_gravity_magnitude_m_s2, 0.0);
  EXPECT_GT(probe.telemetry().last_atmosphere_density_kg_m3, 0.0);
  EXPECT_EQ(probe.probeState().consecutive_failures, 0u);

  EXPECT_EQ(probe.probeTick(), 0u);
  EXPECT_EQ(probe.telemetry().last_point_index, 1u);
  EXPECT_DOUBLE_EQ(probe.telemetry().last_lat_deg, -10.0);

  EXPECT_EQ(probe.probeTick(), 0u);
  EXPECT_EQ(probe.telemetry().last_point_index, 0u);
  EXPECT_EQ(probe.probeState().tick_count, 3u);
}

/* ----------------------------- Vacuum semantics ----------------------------- */

/** @test A vacuum body's zero density is a successful query. */
TEST(WorldQueryProbe, VacuumBodySucceedsWithZeroDensity) {
  CelestialBody moon;
  moon.tunables().get() = analyticMoon();
  ASSERT_EQ(moon.init(), 0u);

  WorldQueryProbe probe;
  probe.setBody(&moon);
  configureSurvey(probe, 1u);

  EXPECT_EQ(probe.probeTick(), 0u);
  EXPECT_EQ(probe.telemetry().last_query_succeeded, 1u);
  EXPECT_DOUBLE_EQ(probe.telemetry().last_atmosphere_density_kg_m3, 0.0);
  EXPECT_GT(probe.telemetry().last_gravity_magnitude_m_s2, 0.0);
}
