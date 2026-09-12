/**
 * @file RoverControllerClosedLoop_uTest.cpp
 * @brief Closed-loop tests: RoverController <-> GroundVehicle on flat terrain.
 *
 * Fixture: analytic Earth (ellipsoid terrain, no data files), the rover
 * at the grid anchor, the controller handed to the plant through the
 * drive-command seam. Both step at 10 Hz; the controller reads the
 * previous tick's pose and the plant consumes the command it writes,
 * the order the demo scheduler runs them in.
 *
 * Coverage:
 *   - The seam: an attached but not-yet-driving block leaves the plant
 *     on its built-in trajectory; a valid block replaces it.
 *   - HOLD keeps the rover at rest; TRAJECTORY reproduces the plant's
 *     own circle.
 *   - WAYPOINT: a leg north arrives inside tolerance without overshoot
 *     and latches ARRIVED with the rover at rest; a leg east from a
 *     north heading steers clockwise and crawls while turning; a new
 *     target clears ARRIVED and is reached in turn.
 */

#include "demos/apex_horizon_demo/rover_controller/inc/RoverController.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/factory/inc/Body.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFidelity.hpp"

#include <cmath>

#include <gtest/gtest.h>

using appsim::ground_vehicle::GroundVehicle;
using appsim::rover_controller::DriveMode;
using appsim::rover_controller::RoverController;
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

/// Rover at the anchor, controller attached through the seam.
struct Rig {
  CelestialBody earth;
  GroundVehicle rover;
  RoverController ctl;

  explicit Rig(double init_heading_deg = 0.0) {
    earth.tunables().set(analyticEarth());
    EXPECT_EQ(earth.init(), 0u);
    auto& rp = rover.tunables().get();
    rp.init_lat_deg = 39.5;
    rp.init_lon_deg = -105.5;
    rp.init_heading_deg = init_heading_deg;
    rover.setBody(&earth);
    ctl.setVehicle(&rover);
    rover.setDriveCommand(ctl.driveCommand());
  }

  /// One scheduler tick: controller then plant.
  void tick() {
    (void)ctl.controllerStep();
    (void)rover.vehicleStep();
  }
  void run(int ticks) {
    for (int i = 0; i < ticks; ++i) {
      tick();
    }
  }
  void north_east(double& n, double& e) const { ctl.gridPosition(n, e); }
};

} // namespace

/* ----------------------------- The seam ----------------------------- */

TEST(RoverControllerSeam, InvalidBlockLeavesTrajectoryDriving) {
  Rig rig(45.0);
  // Never step the controller: its block stays valid = 0.
  for (int i = 0; i < 20; ++i) {
    (void)rig.rover.vehicleStep();
  }
  // Built-in trajectory: heading advanced at steer_rate_deg_s (6 deg/s)
  // for 2 s from 45 deg, speed rising toward 0.6 * max.
  EXPECT_NEAR(rig.rover.telemetry().heading_deg, 45.0 + 6.0 * 2.0, 1e-6);
  EXPECT_GT(rig.rover.telemetry().speed_m_s, 0.5);
}

TEST(RoverControllerSeam, ValidBlockReplacesTrajectory) {
  Rig rig(45.0);
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::HOLD);
  rig.run(20);
  EXPECT_EQ(rig.ctl.controllerOutput().valid, 1u);
  // HOLD: no steer, no throttle -> heading fixed, speed stays at rest.
  EXPECT_NEAR(rig.rover.telemetry().heading_deg, 45.0, 1e-9);
  EXPECT_NEAR(rig.rover.telemetry().speed_m_s, 0.0, 1e-9);
}

TEST(RoverControllerSeam, TrajectoryModeReproducesThePlantsCircle) {
  Rig rig(45.0);
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::TRAJECTORY);
  rig.run(20);
  EXPECT_NEAR(rig.rover.telemetry().heading_deg, 45.0 + 6.0 * 2.0, 1e-6);
  EXPECT_GT(rig.rover.telemetry().speed_m_s, 0.5);
}

/* ----------------------------- WAYPOINT ----------------------------- */

TEST(RoverControllerWaypoint, LegNorthArrivesAndLatches) {
  Rig rig(0.0);
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::WAYPOINT);
  rig.tick(); // plant latches its pose; controller has an origin
  rig.ctl.setTargetRel(10.0, 0.0);
  ASSERT_EQ(rig.ctl.controllerState().target_valid, 1u);

  double max_north = 0.0;
  for (int i = 0; i < 300; ++i) { // 30 s
    rig.tick();
    double n = 0.0, e = 0.0;
    rig.north_east(n, e);
    max_north = std::max(max_north, n);
  }
  double n = 0.0, e = 0.0;
  rig.north_east(n, e);
  const auto& out = rig.ctl.controllerOutput();
  EXPECT_EQ(out.arrived, 1u) << "dist=" << out.distance_m;
  EXPECT_NEAR(n, 10.0, 0.5) << "north";
  EXPECT_NEAR(e, 0.0, 0.5) << "east";
  EXPECT_LT(max_north, 10.5) << "overshoot";
  EXPECT_LT(rig.rover.telemetry().speed_m_s, 0.2) << "at rest after arrival";
  EXPECT_NEAR(out.steer_rate_deg_s, 0.0, 1e-9);
  EXPECT_NEAR(out.throttle_frac, 0.0, 1e-9);
}

TEST(RoverControllerWaypoint, LegEastSteersClockwiseAndCrawlsWhileTurning) {
  Rig rig(0.0); // heading north
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::WAYPOINT);
  rig.tick();
  rig.ctl.setTargetRel(0.0, 6.0);
  rig.tick();
  const auto& out = rig.ctl.controllerOutput();
  EXPECT_NEAR(out.heading_error_deg, 90.0, 1.0);
  EXPECT_GT(out.steer_rate_deg_s, 0.0) << "east of a north heading is clockwise";
  EXPECT_NEAR(out.throttle_frac, rig.ctl.tunables().get().turn_throttle_frac, 1e-9);

  rig.run(400); // 40 s
  double n = 0.0, e = 0.0;
  rig.north_east(n, e);
  EXPECT_EQ(out.arrived, 1u) << "dist=" << out.distance_m;
  EXPECT_NEAR(e, 6.0, 0.5);
  EXPECT_NEAR(n, 0.0, 1.0);
}

TEST(RoverControllerWaypoint, RetargetClearsArrivedAndReachesTheNextLeg) {
  Rig rig(0.0);
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::WAYPOINT);
  rig.tick();
  rig.ctl.setTargetRel(5.0, 0.0);
  rig.run(300);
  ASSERT_EQ(rig.ctl.controllerOutput().arrived, 1u);
  const std::uint16_t SEQ1 = rig.ctl.controllerState().target_seq;

  rig.ctl.setTargetRel(3.05, 0.0); // 10 ft further north
  EXPECT_EQ(rig.ctl.controllerOutput().arrived, 0u);
  EXPECT_EQ(rig.ctl.controllerState().target_seq, SEQ1 + 1u);
  rig.run(300);
  double n = 0.0, e = 0.0;
  rig.north_east(n, e);
  EXPECT_EQ(rig.ctl.controllerOutput().arrived, 1u);
  EXPECT_NEAR(n, 8.05, 0.6);
}

TEST(RoverControllerWaypoint, HoldModeIgnoresTheTarget) {
  Rig rig(0.0);
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::HOLD);
  rig.tick();
  rig.ctl.setTargetRel(10.0, 0.0);
  rig.run(100);
  double n = 0.0, e = 0.0;
  rig.north_east(n, e);
  EXPECT_NEAR(n, 0.0, 1e-6);
  EXPECT_NEAR(rig.rover.telemetry().speed_m_s, 0.0, 1e-9);
}
