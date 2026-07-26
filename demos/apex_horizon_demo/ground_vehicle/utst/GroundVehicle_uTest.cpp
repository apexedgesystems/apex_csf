/**
 * @file GroundVehicle_uTest.cpp
 * @brief Unit tests for the GroundVehicle demo component.
 *
 * Covers:
 *   - The frozen ROVR/1 wire layout: total size and every field offset
 *     pinned, so drift fails here before any pairing.
 *   - Not-ready body: steps count, nothing else moves.
 *   - Init-pose latch on the first ready tick.
 *   - Kinematics on flat analytic terrain: first-order speed approach,
 *     heading wrap, lat/lon integration against hand-computed values.
 *   - Flat-terrain invariants: surface clamp, zero slope, no lidar
 *     hits, ray-count clamping to MAX_LIDAR_RAYS.
 *
 * Terrain here is the analytic ellipsoid (flat, no data files); the
 * slope/lidar positive cases ride the demo's htile-backed smoke.
 */

#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicle.hpp"

#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/factory/inc/Body.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFidelity.hpp"
#include "src/utilities/math/vecmat/inc/Angles.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstring>

using appsim::ground_vehicle::GroundVehicle;
using appsim::ground_vehicle::GroundVehicleTelemetry;
using appsim::ground_vehicle::GroundVehicleTunables;
using appsim::ground_vehicle::MAX_LIDAR_RAYS;
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

void configureRover(GroundVehicle& rover) {
  auto& p = rover.tunables().get();
  p.max_speed_m_s = 8.0;
  p.throttle_default = 0.5;
  p.steer_rate_deg_s = 0.0;
  p.init_lat_deg = 39.5;
  p.init_lon_deg = -105.5;
  p.init_heading_deg = 90.0; // due east
  p.max_slope_deg = 25.0;
  p.lidar_n_rays = 4u;
  p.lidar_max_range_m = 100.0;
  p.lidar_fov_deg = 90.0;
  p.lidar_step_m = 5.0;
  std::strncpy(p.body_label, "test_rover", sizeof(p.body_label) - 1);
}

} // namespace

/* ----------------------------- Wire layout ----------------------------- */

/** @test The ROVR/1 wire layout is frozen: 256 bytes, every offset pinned. */
TEST(GroundVehicleWire, RoverFrameLayoutIsFrozen) {
  static_assert(sizeof(GroundVehicleTelemetry) == 256u);
  static_assert(MAX_LIDAR_RAYS == 16u);
  static_assert(offsetof(GroundVehicleTelemetry, timestamp_ns) == 0u);
  static_assert(offsetof(GroundVehicleTelemetry, tick) == 8u);
  static_assert(offsetof(GroundVehicleTelemetry, pos_lat_deg) == 16u);
  static_assert(offsetof(GroundVehicleTelemetry, pos_lon_deg) == 24u);
  static_assert(offsetof(GroundVehicleTelemetry, pos_alt_m) == 32u);
  static_assert(offsetof(GroundVehicleTelemetry, heading_deg) == 40u);
  static_assert(offsetof(GroundVehicleTelemetry, speed_m_s) == 48u);
  static_assert(offsetof(GroundVehicleTelemetry, ground_elevation_m) == 56u);
  static_assert(offsetof(GroundVehicleTelemetry, slope_deg) == 64u);
  static_assert(offsetof(GroundVehicleTelemetry, slope_azimuth_deg) == 72u);
  static_assert(offsetof(GroundVehicleTelemetry, is_slipping) == 80u);
  static_assert(offsetof(GroundVehicleTelemetry, is_off_terrain) == 81u);
  static_assert(offsetof(GroundVehicleTelemetry, reserved0) == 82u);
  static_assert(offsetof(GroundVehicleTelemetry, lidar_n_rays) == 84u);
  static_assert(offsetof(GroundVehicleTelemetry, lidar_range_m) == 88u);
  static_assert(offsetof(GroundVehicleTelemetry, lidar_hit) == 216u);
  static_assert(offsetof(GroundVehicleTelemetry, reserved1) == 232u);
  SUCCEED();
}

/* ----------------------------- Not-ready body ----------------------------- */

/** @test With no ready body the step only counts ticks. */
TEST(GroundVehicle, NotReadyBodyOnlyCountsTicks) {
  GroundVehicle rover;
  configureRover(rover);

  EXPECT_EQ(rover.vehicleStep(), 0u);
  EXPECT_EQ(rover.vehicleState().tick_count, 1u);
  EXPECT_EQ(rover.vehicleState().initialized, 0u);
  EXPECT_DOUBLE_EQ(rover.telemetry().speed_m_s, 0.0);
}

/* ----------------------------- Init latch + kinematics ----------------------------- */

/** @test First ready tick latches the init pose from tunables. */
TEST(GroundVehicle, FirstTickLatchesInitPose) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  GroundVehicle rover;
  rover.setBody(&earth);
  configureRover(rover);

  EXPECT_EQ(rover.vehicleStep(), 0u);
  EXPECT_EQ(rover.vehicleState().initialized, 1u);
  EXPECT_NEAR(rover.telemetry().pos_lat_deg, 39.5, 1e-3);
  EXPECT_DOUBLE_EQ(rover.telemetry().heading_deg, 90.0);
}

/** @test Speed approaches throttle*max first-order; heading holds; the
 *  eastward step moves longitude by the hand-computed local-flat delta. */
TEST(GroundVehicle, FlatKinematicsMatchClosedForm) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);
  const double R = earth.telemetry().reference_radius_m;
  ASSERT_GT(R, 0.0);

  GroundVehicle rover;
  rover.setBody(&earth);
  configureRover(rover);

  // Tick 1: latch + first integration step.
  EXPECT_EQ(rover.vehicleStep(), 0u);
  constexpr double DT = 0.1;
  constexpr double TAU = 1.0;
  const double TARGET = 0.5 * 8.0;
  const double V1 = TARGET * (DT / TAU); // from rest, one step
  EXPECT_NEAR(rover.telemetry().speed_m_s, V1, 1e-12);
  EXPECT_DOUBLE_EQ(rover.telemetry().heading_deg, 90.0);

  // Longitude moved east by v*dt / m_per_deg_lon; latitude untouched.
  const double LAT_RAD = rover.telemetry().pos_lat_deg * apex::math::vecmat::DEG_TO_RAD;
  const double M_PER_DEG_LON = R * std::cos(LAT_RAD) * apex::math::vecmat::DEG_TO_RAD;
  const double EXPECT_DLON = (V1 * DT) / M_PER_DEG_LON;
  EXPECT_NEAR(rover.telemetry().pos_lon_deg, -105.5 + EXPECT_DLON, 1e-9);
  EXPECT_NEAR(rover.telemetry().pos_lat_deg, 39.5, 1e-9);

  // Tick 2: speed continues the first-order approach.
  EXPECT_EQ(rover.vehicleStep(), 0u);
  const double V2 = V1 + (TARGET - V1) * (DT / TAU);
  EXPECT_NEAR(rover.telemetry().speed_m_s, V2, 1e-12);
}

/** @test Heading wraps through 360 with a steering rate applied. */
TEST(GroundVehicle, HeadingWrapsUnderSteering) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  GroundVehicle rover;
  rover.setBody(&earth);
  configureRover(rover);
  rover.tunables().get().init_heading_deg = 359.5;
  rover.tunables().get().steer_rate_deg_s = 10.0; // +1 deg per 0.1 s tick

  EXPECT_EQ(rover.vehicleStep(), 0u);
  EXPECT_NEAR(rover.telemetry().heading_deg, 0.5, 1e-9);
}

/* ----------------------------- Flat-terrain invariants ----------------------------- */

/** @test On flat analytic terrain: clamped to the surface, no slope, no
 *  slip, no lidar hits, ray count clamped to the wire maximum. */
TEST(GroundVehicle, FlatTerrainInvariants) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  GroundVehicle rover;
  rover.setBody(&earth);
  configureRover(rover);
  rover.tunables().get().lidar_n_rays = 64u; // beyond the wire maximum

  EXPECT_EQ(rover.vehicleStep(), 0u);
  const GroundVehicleTelemetry& TLM = rover.telemetry();

  EXPECT_EQ(TLM.is_off_terrain, 0u);
  EXPECT_DOUBLE_EQ(TLM.pos_alt_m, TLM.ground_elevation_m);
  EXPECT_DOUBLE_EQ(TLM.slope_deg, 0.0);
  EXPECT_EQ(TLM.is_slipping, 0u);
  EXPECT_EQ(TLM.lidar_n_rays, MAX_LIDAR_RAYS);
  for (std::uint32_t i = 0; i < MAX_LIDAR_RAYS; ++i) {
    EXPECT_EQ(TLM.lidar_hit[i], 0u);
    EXPECT_DOUBLE_EQ(TLM.lidar_range_m[i], 100.0);
  }
}
