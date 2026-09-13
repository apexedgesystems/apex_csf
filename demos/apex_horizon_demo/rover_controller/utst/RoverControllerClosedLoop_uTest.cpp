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
#include <cstring>
#include <vector>

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

TEST(RoverControllerSeam, TrajectoryModeDrivesASteeredCircle) {
  Rig rig(45.0);
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::TRAJECTORY);
  rig.tick();
  // First tick: the steering angle is commanded but the rover has only
  // just started rolling (0.15 m/s after one 10 Hz step), so the heading
  // has barely moved -- a steered plant cannot pivot at rest.
  EXPECT_NEAR(rig.ctl.controllerOutput().steer_angle_deg, 5.0, 1e-9);
  EXPECT_NEAR(rig.rover.telemetry().heading_deg, 45.0, 0.1);
  const double H1 = rig.rover.telemetry().heading_deg;
  rig.run(200); // 20 s
  const double V = rig.rover.telemetry().speed_m_s;
  EXPECT_GT(V, 4.0) << "cruising at 0.6 of max";
  // At 4.8 m/s the heading turns at v tan(5 deg) / 1.5 m = 16 deg/s: the
  // last second of the run must show that rate.
  const double H2 = rig.rover.telemetry().heading_deg;
  rig.run(10);
  double dh = rig.rover.telemetry().heading_deg - H2;
  if (dh < 0.0)
    dh += 360.0;
  EXPECT_NEAR(
      dh, V * std::tan(5.0 * apex::math::vecmat::DEG_TO_RAD) / 1.5 * apex::math::vecmat::RAD_TO_DEG,
      0.5);
  EXPECT_NE(H2, H1);
}

/* ----------------------------- WAYPOINT ----------------------------- */

TEST(RoverControllerWaypoint, LegNorthRampsCruisesBrakesAndLatches) {
  Rig rig(0.0);
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::WAYPOINT);
  rig.tick(); // plant latches its pose; controller has an origin
  rig.ctl.setTargetRel(20.0, 0.0);
  ASSERT_EQ(rig.ctl.controllerState().target_valid, 1u);

  double max_north = 0.0, max_v = 0.0;
  int arrived_tick = -1;
  for (int i = 0; i < 400; ++i) { // 40 s
    rig.tick();
    double n = 0.0, e = 0.0;
    rig.north_east(n, e);
    max_north = std::max(max_north, n);
    max_v = std::max(max_v, rig.rover.telemetry().speed_m_s);
    if (arrived_tick < 0 && rig.ctl.controllerOutput().arrived != 0u) {
      arrived_tick = i;
    }
  }
  double n = 0.0, e = 0.0;
  rig.north_east(n, e);
  const auto& out = rig.ctl.controllerOutput();
  EXPECT_EQ(out.arrived, 1u) << "dist=" << out.distance_m;
  EXPECT_GE(arrived_tick, 0);
  EXPECT_LT(arrived_tick, 150) << "20 m at 3 m/s cruise with ramps: under 15 s";
  EXPECT_NEAR(max_v, 3.0, 0.1) << "cruise";
  EXPECT_NEAR(n, 20.0, 0.3) << "north";
  EXPECT_NEAR(e, 0.0, 0.05) << "east (a straight leg)";
  EXPECT_LT(max_north, 20.3) << "braking profile lands without overshoot";
  EXPECT_LT(rig.rover.telemetry().speed_m_s, 0.05) << "at rest after arrival";
  EXPECT_NEAR(out.throttle_frac, 0.0, 1e-9);
}

TEST(RoverControllerWaypoint, LegEastArcsRightWithoutPivoting) {
  Rig rig(0.0); // heading north
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::WAYPOINT);
  rig.tick();
  rig.ctl.setTargetRel(0.0, 12.0);
  rig.tick();
  const auto& out = rig.ctl.controllerOutput();
  EXPECT_NEAR(out.heading_error_deg, 90.0, 1.0);
  EXPECT_NEAR(out.steer_angle_deg, 33.0, 1e-9) << "full right lock toward a target abeam";
  EXPECT_LT(rig.rover.telemetry().heading_deg, 0.5)
      << "no pivot: one 10 Hz step at 0.15 m/s turns well under a degree";

  // The heading may only change while the rover rolls, and never faster
  // than the plant's minimum radius allows.
  double prev_h = rig.rover.telemetry().heading_deg;
  for (int i = 0; i < 400; ++i) {
    rig.tick();
    const double V = rig.rover.telemetry().speed_m_s;
    double dh = rig.rover.telemetry().heading_deg - prev_h;
    if (dh > 180.0)
      dh -= 360.0;
    if (dh < -180.0)
      dh += 360.0;
    prev_h = rig.rover.telemetry().heading_deg;
    const double MAX_RATE = V * std::tan(33.0 * apex::math::vecmat::DEG_TO_RAD) / 1.5 *
                                apex::math::vecmat::RAD_TO_DEG * 0.1 +
                            1e-6;
    EXPECT_LE(std::fabs(dh), MAX_RATE) << "tick " << i << " v=" << V;
  }
  double n = 0.0, e = 0.0;
  rig.north_east(n, e);
  EXPECT_EQ(out.arrived, 1u) << "dist=" << out.distance_m;
  EXPECT_NEAR(e, 12.0, 0.3);
  EXPECT_NEAR(n, 0.0, 0.3);
}

TEST(RoverControllerWaypoint, RetargetClearsArrivedAndReachesTheNextLeg) {
  Rig rig(0.0);
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::WAYPOINT);
  rig.tick();
  rig.ctl.setTargetRel(10.0, 0.0);
  rig.run(300);
  ASSERT_EQ(rig.ctl.controllerOutput().arrived, 1u);
  const std::uint16_t SEQ1 = rig.ctl.controllerState().target_seq;

  rig.ctl.setTargetRel(10.0, 0.0); // 10 m further north
  EXPECT_EQ(rig.ctl.controllerOutput().arrived, 0u);
  EXPECT_EQ(rig.ctl.controllerState().target_seq, SEQ1 + 1u);
  rig.run(300);
  double n = 0.0, e = 0.0;
  rig.north_east(n, e);
  EXPECT_EQ(rig.ctl.controllerOutput().arrived, 1u);
  EXPECT_NEAR(n, 20.0, 0.4);
}

TEST(RoverControllerWaypoint, ShortStraightLegLandsInsideTolerance) {
  // A 5 ft leg straight ahead: the precision case -- braking profile
  // from a standing start, arrival inside 0.2 m with centimetre overshoot.
  Rig rig(0.0);
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::WAYPOINT);
  rig.tick();
  rig.ctl.setTargetRel(1.524, 0.0);
  double max_north = 0.0;
  int arrived_tick = -1;
  for (int i = 0; i < 200; ++i) { // 20 s
    rig.tick();
    double n = 0.0, e = 0.0;
    rig.north_east(n, e);
    max_north = std::max(max_north, n);
    if (arrived_tick < 0 && rig.ctl.controllerOutput().arrived != 0u) {
      arrived_tick = i;
    }
  }
  double n = 0.0, e = 0.0;
  rig.north_east(n, e);
  EXPECT_GE(arrived_tick, 0);
  EXPECT_LT(arrived_tick, 60) << "arrives within 6 s";
  EXPECT_NEAR(n, 1.524, 0.2);
  EXPECT_LT(max_north, 1.524 + 0.15) << "overshoot";
  EXPECT_LT(rig.rover.telemetry().speed_m_s, 0.05);
}

TEST(RoverControllerWaypoint, RepeatedLegsFromAnOffAxisHeadingEndSquare) {
  // Five consecutive "10 m north of here" legs from a rover facing
  // south-east (the operator clicking the same sequence): the first
  // leg swings onto the line; every later leg starts and ends within a
  // few degrees of north, on the line, because the controller follows
  // the leg line and drives the last metres straight along it.
  Rig rig(116.0);
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::WAYPOINT);
  rig.tick();
  double start_e = 0.0, unused = 0.0;
  rig.north_east(unused, start_e);
  for (int leg = 1; leg <= 5; ++leg) {
    double n0 = 0.0, e0 = 0.0;
    rig.north_east(n0, e0);
    rig.ctl.setTargetRel(10.0, 0.0);
    int ticks = 0;
    while (rig.ctl.controllerOutput().arrived == 0u && ticks < 600) {
      rig.tick();
      ++ticks;
    }
    ASSERT_LT(ticks, 600) << "leg " << leg << " never arrived";
    double n1 = 0.0, e1 = 0.0;
    rig.north_east(n1, e1);
    const double H = rig.rover.telemetry().heading_deg;
    const double H_ERR = std::fabs(std::fmod(H + 180.0, 360.0) - 180.0);
    EXPECT_NEAR(n1 - n0, 10.0, 0.3) << "leg " << leg;
    EXPECT_NEAR(e1, start_e, 0.3) << "leg " << leg << " lands on the line";
    EXPECT_LT(H_ERR, (leg == 1) ? 10.0 : 2.0)
        << "leg " << leg << " ends square (heading " << H << ")";
    if (leg >= 2) {
      EXPECT_LT(ticks, 50) << "leg " << leg << " is a straight 10 m at 3 m/s, no re-turn";
    }
  }
}

TEST(RoverControllerWaypoint, StraightReversalTurnsAroundInsteadOfDrivingAway) {
  // Out 20 m east, then 20 m straight back: the aim point starts
  // directly behind the rover (180 deg error, where pursuit curvature
  // is zero). The rover must commit to a turn, come back along the
  // line, and land on the start without first driving off east.
  Rig rig(90.0); // heading east
  rig.ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::WAYPOINT);
  rig.tick();
  rig.ctl.setTargetRel(0.0, 20.0);
  int ticks = 0;
  while (rig.ctl.controllerOutput().arrived == 0u && ticks < 400) {
    rig.tick();
    ++ticks;
  }
  ASSERT_LT(ticks, 400);
  rig.ctl.setTargetRel(0.0, -20.0);
  double max_e = 0.0;
  double max_rate = 0.0;
  double prev_h = rig.rover.telemetry().heading_deg;
  ticks = 0;
  while (rig.ctl.controllerOutput().arrived == 0u && ticks < 400) {
    rig.tick();
    ++ticks;
    double n = 0.0, e = 0.0;
    rig.north_east(n, e);
    max_e = std::max(max_e, e);
    double dh = rig.rover.telemetry().heading_deg - prev_h;
    if (dh > 180.0)
      dh -= 360.0;
    if (dh < -180.0)
      dh += 360.0;
    prev_h = rig.rover.telemetry().heading_deg;
    max_rate = std::max(max_rate, std::fabs(dh) * 10.0);
  }
  double n = 0.0, e = 0.0;
  rig.north_east(n, e);
  ASSERT_LT(ticks, 400) << "never arrived";
  EXPECT_LT(ticks, 320) << "reversal: a 12 s turn at 15 deg/s plus a 20 m leg, not a stall";
  EXPECT_LT(max_e, 20.0 + 3.0) << "does not drive away before turning";
  EXPECT_LE(max_rate, 15.0 + 0.5) << "the yaw-rate bound holds through the reversal";
  EXPECT_NEAR(e, 0.0, 0.5);
  EXPECT_NEAR(n, 0.0, 0.5);
  const double H = rig.rover.telemetry().heading_deg;
  EXPECT_NEAR(H, 270.0, 5.0) << "arrives aligned with the return leg";
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

/* ----------------------------- Wire adoption ----------------------------- */

TEST(RoverControllerAdoption, CommandedModeAndTargetAreAdoptedEdgeTriggered) {
  using appsim::ground_vehicle::RoverCmdSetTarget;
  using appsim::ground_vehicle::RoverOpcode;
  Rig rig(0.0);
  rig.tick();
  auto send = [&](RoverOpcode op, const std::vector<std::uint8_t>& bytes) {
    apex::compat::rospan<std::uint8_t> payload(bytes.data(), bytes.size());
    std::vector<std::uint8_t> resp;
    return rig.rover.handleCommand(static_cast<std::uint16_t>(op), payload, resp);
  };
  RoverCmdSetTarget t{8.0F, 0.0F};
  std::vector<std::uint8_t> tb(sizeof(t));
  std::memcpy(tb.data(), &t, sizeof(t));

  ASSERT_EQ(send(RoverOpcode::SET_MODE, {2u}), 0u);
  ASSERT_EQ(send(RoverOpcode::SET_TARGET_REL, tb), 0u);
  rig.tick();
  EXPECT_EQ(rig.ctl.mode(), DriveMode::WAYPOINT);
  EXPECT_EQ(rig.ctl.controllerState().target_valid, 1u);
  EXPECT_EQ(rig.rover.frameBytes()[appsim::ground_vehicle::FB_CONTROLLER_MODE], 2u);

  rig.run(300);
  double n = 0.0, e = 0.0;
  rig.north_east(n, e);
  EXPECT_EQ(rig.ctl.controllerOutput().arrived, 1u);
  EXPECT_NEAR(n, 8.0, 0.5);

  // A direct setMode between commands keeps control: the wire's value
  // was already adopted, so it is not re-applied.
  rig.ctl.setMode(DriveMode::HOLD);
  rig.tick();
  EXPECT_EQ(rig.ctl.mode(), DriveMode::HOLD);
}
