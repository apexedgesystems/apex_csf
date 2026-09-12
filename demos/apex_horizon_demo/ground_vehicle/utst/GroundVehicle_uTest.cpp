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
 *   - Drive commands: HALT coast-down (closed-form decay) with frozen
 *     heading, RESUME recovery, SET_THROTTLE retarget, and payload
 *     validation (short / out-of-range / unknown opcode).
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
#include <vector>

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

/* ----------------------------- Drive commands ----------------------------- */

namespace {

/// Sends a command opcode with the given payload bytes; returns the
/// handler's result code.
std::uint8_t sendCmd(GroundVehicle& rover, std::uint16_t opcode,
                     const std::uint8_t* bytes = nullptr, std::size_t len = 0) {
  static const std::uint8_t NONE[1] = {0};
  apex::compat::rospan<std::uint8_t> payload(bytes != nullptr ? bytes : NONE, len);
  std::vector<std::uint8_t> response;
  return rover.handleCommand(opcode, payload, response);
}

std::uint8_t sendCmd(GroundVehicle& rover, GroundVehicle::DriveCmd cmd,
                     const std::uint8_t* bytes = nullptr, std::size_t len = 0) {
  return sendCmd(rover, static_cast<std::uint16_t>(cmd), bytes, len);
}

} // namespace

/** @test HALT decays speed by the first-order closed form and freezes
 *  heading; RESUME restores the default-throttle target. */
TEST(GroundVehicleCmd, HaltCoastsDownThenResumeRecovers) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  GroundVehicle rover;
  rover.setBody(&earth);
  configureRover(rover);
  rover.tunables().get().steer_rate_deg_s = 6.0;

  // Build up speed: s' = 0.9 s + 0.1 T per 10 Hz step (DT/TAU = 0.1).
  constexpr double TARGET = 0.5 * 8.0;
  for (int i = 0; i < 20; ++i) {
    ASSERT_EQ(rover.vehicleStep(), 0u);
  }
  const double V_CRUISE = rover.telemetry().speed_m_s;
  EXPECT_NEAR(V_CRUISE, TARGET * (1.0 - std::pow(0.9, 20)), 1e-12);

  EXPECT_EQ(sendCmd(rover, GroundVehicle::DriveCmd::HALT), 0u);
  const double HEADING_AT_HALT = rover.telemetry().heading_deg;
  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(rover.vehicleStep(), 0u);
  }
  EXPECT_NEAR(rover.telemetry().speed_m_s, V_CRUISE * std::pow(0.9, 10), 1e-12);
  EXPECT_DOUBLE_EQ(rover.telemetry().heading_deg, HEADING_AT_HALT);

  EXPECT_EQ(sendCmd(rover, GroundVehicle::DriveCmd::RESUME), 0u);
  const double V_HALTED = rover.telemetry().speed_m_s;
  ASSERT_EQ(rover.vehicleStep(), 0u);
  EXPECT_NEAR(rover.telemetry().speed_m_s, 0.9 * V_HALTED + 0.1 * TARGET, 1e-12);
  EXPECT_NE(rover.telemetry().heading_deg, HEADING_AT_HALT); // steering resumed
}

/** @test SET_THROTTLE retargets the speed loop; RESUME clears the
 *  override back to the default throttle. */
TEST(GroundVehicleCmd, SetThrottleOverridesDefault) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  GroundVehicle rover;
  rover.setBody(&earth);
  configureRover(rover);

  const std::uint8_t PCT = 25u; // 25% of 8 m/s = 2 m/s target
  EXPECT_EQ(sendCmd(rover, GroundVehicle::DriveCmd::SET_THROTTLE, &PCT, 1), 0u);
  for (int i = 0; i < 200; ++i) {
    ASSERT_EQ(rover.vehicleStep(), 0u);
  }
  EXPECT_NEAR(rover.telemetry().speed_m_s, 2.0, 1e-6);

  EXPECT_EQ(sendCmd(rover, GroundVehicle::DriveCmd::RESUME), 0u);
  for (int i = 0; i < 200; ++i) {
    ASSERT_EQ(rover.vehicleStep(), 0u);
  }
  EXPECT_NEAR(rover.telemetry().speed_m_s, 4.0, 1e-6);
}

/** @test Malformed drive commands are rejected with the documented
 *  codes and leave the drive state untouched. */
TEST(GroundVehicleCmd, ValidationRejectsBadPayloads) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  GroundVehicle rover;
  rover.setBody(&earth);
  configureRover(rover);

  using system_core::system_component::CommandResult;
  // SET_THROTTLE with no payload.
  EXPECT_EQ(sendCmd(rover, GroundVehicle::DriveCmd::SET_THROTTLE),
            static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD));
  // SET_THROTTLE out of range.
  const std::uint8_t BAD = 101u;
  EXPECT_EQ(sendCmd(rover, GroundVehicle::DriveCmd::SET_THROTTLE, &BAD, 1),
            static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT));
  // Unknown opcode falls through to the base handler (non-SUCCESS).
  EXPECT_NE(sendCmd(rover, static_cast<std::uint16_t>(0x0999u)), 0u);

  // Rejected commands changed nothing: vehicle still targets default.
  for (int i = 0; i < 200; ++i) {
    ASSERT_EQ(rover.vehicleStep(), 0u);
  }
  EXPECT_NEAR(rover.telemetry().speed_m_s, 4.0, 1e-6);
}

/** @test Timestamps sit on the sim tick grid: consecutive published
 *  frames differ by exactly one tick period (100 ms at 10 Hz). */
TEST(GroundVehicle, TimestampsOnTickGrid) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  GroundVehicle rover;
  rover.setBody(&earth);
  configureRover(rover);

  ASSERT_EQ(rover.vehicleStep(), 0u);
  const std::uint64_t T1 = rover.telemetry().timestamp_ns;
  for (int i = 0; i < 5; ++i) {
    ASSERT_EQ(rover.vehicleStep(), 0u);
  }
  const std::uint64_t T2 = rover.telemetry().timestamp_ns;
  EXPECT_EQ(T2 - T1, 5u * 100'000'000u); // 5 ticks at 10 Hz, exact
}

/* ----------------------------- Grid boot ----------------------------- */

TEST(GroundVehicle, GridBootPlacesTheRoverFromTheAnchor) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);
  GroundVehicle rover;
  configureRover(rover);
  auto& p = rover.tunables().get();
  p.init_from_grid = 1u;
  p.anchor_lat_deg = 39.5;
  p.anchor_lon_deg = -105.5;
  p.init_north_m = 100.0;
  p.init_east_m = -50.0;
  p.init_heading_deg = 0.0;
  p.throttle_default = 0.0; // sit still after the latch
  p.steer_rate_deg_s = 0.0;
  rover.setBody(&earth);
  (void)rover.vehicleStep();

  const double R = earth.telemetry().reference_radius_m;
  const double M_PER_DEG_LAT = R * apex::math::vecmat::DEG_TO_RAD;
  const double M_PER_DEG_LON =
      R * std::cos(39.5 * apex::math::vecmat::DEG_TO_RAD) * apex::math::vecmat::DEG_TO_RAD;
  EXPECT_NEAR(rover.telemetry().pos_lat_deg, 39.5 + 100.0 / M_PER_DEG_LAT, 1e-9);
  EXPECT_NEAR(rover.telemetry().pos_lon_deg, -105.5 - 50.0 / M_PER_DEG_LON, 1e-9);
}

/* ----------------------------- ROVR/2 command surface ----------------------------- */

namespace {

using appsim::ground_vehicle::CmdResultCode;
using appsim::ground_vehicle::RoverCmdSetLed;
using appsim::ground_vehicle::RoverCmdSetTarget;
using appsim::ground_vehicle::RoverOpcode;
namespace fb = appsim::ground_vehicle;

std::uint8_t sendBytes(GroundVehicle& rover, RoverOpcode op,
                       const std::vector<std::uint8_t>& bytes) {
  apex::compat::rospan<std::uint8_t> payload(bytes.data(), bytes.size());
  std::vector<std::uint8_t> resp;
  return rover.handleCommand(static_cast<std::uint16_t>(op), payload, resp);
}

std::vector<std::uint8_t> targetBytes(float a, float b) {
  RoverCmdSetTarget t{a, b};
  std::vector<std::uint8_t> v(sizeof(t));
  std::memcpy(v.data(), &t, sizeof(t));
  return v;
}

/// Ready rover, one tick in (pose latched, frame stamped).
struct ReadyRover {
  CelestialBody earth;
  GroundVehicle rover;
  ReadyRover() {
    earth.tunables().set(analyticEarth());
    EXPECT_EQ(earth.init(), 0u);
    configureRover(rover);
    rover.setBody(&earth);
    (void)rover.vehicleStep();
  }
  const std::uint8_t* frame() {
    (void)rover.vehicleStep();
    return rover.frameBytes();
  }
};

} // namespace

TEST(GroundVehicleWire, ReservedTailOffsetsArePinned) {
  static_assert(offsetof(GroundVehicleTelemetry, reserved1) == 232u);
  EXPECT_EQ(232u + fb::FB_BOARD_LINK, 232u);
  EXPECT_EQ(232u + fb::FB_CONTROLLER_MODE, 233u);
  EXPECT_EQ(232u + fb::FB_SEQ_STATE, 234u);
  EXPECT_EQ(232u + fb::FB_ACTIVE_WAYPOINT, 235u);
  EXPECT_EQ(232u + fb::FB_WAYPOINT_TOTAL, 236u);
  EXPECT_EQ(232u + fb::FB_LED_BITS, 237u);
  EXPECT_EQ(232u + fb::FB_LED1_COLOUR, 238u);
  EXPECT_EQ(232u + fb::FB_LED1_RATE, 239u);
  EXPECT_EQ(232u + fb::FB_LED2_COLOUR, 240u);
  EXPECT_EQ(232u + fb::FB_LED2_RATE, 241u);
  EXPECT_EQ(232u + fb::FB_LAST_CMD_RESULT, 242u);
  EXPECT_EQ(232u + fb::FB_LAST_CMD_OPCODE_LO, 243u);
  EXPECT_EQ(232u + fb::FB_LAST_CMD_OPCODE_HI, 244u);
  EXPECT_EQ(232u + fb::FB_BOARD_LOAD_PCT, 245u);
  EXPECT_EQ(232u + fb::FB_BOARD_TICK_LO, 246u);
  EXPECT_EQ(232u + fb::FB_BOARD_TICK_HI, 247u);
  EXPECT_EQ(232u + fb::FB_MAST_PAN_DEG, 248u);
  EXPECT_EQ(232u + fb::FB_MAST_EXT_PCT, 249u);
}

TEST(GroundVehicleCmd, SetModeIsBoundedAndAdoptable) {
  ReadyRover r;
  EXPECT_EQ(sendBytes(r.rover, RoverOpcode::SET_MODE, {2u}), 0u);
  EXPECT_EQ(r.rover.vehicleState().commanded_mode, 2u);
  EXPECT_NE(sendBytes(r.rover, RoverOpcode::SET_MODE, {3u}), 0u);
  EXPECT_EQ(r.rover.vehicleState().commanded_mode, 2u) << "rejected whole";
  EXPECT_NE(sendBytes(r.rover, RoverOpcode::SET_MODE, {}), 0u) << "short payload";
  const auto* f = r.frame();
  EXPECT_EQ(f[fb::FB_LAST_CMD_RESULT],
            static_cast<std::uint8_t>(CmdResultCode::NACK_INVALID_ARGUMENT));
  EXPECT_EQ(f[fb::FB_LAST_CMD_OPCODE_LO], 0x03u);
  EXPECT_EQ(f[fb::FB_LAST_CMD_OPCODE_HI], 0x01u);
}

TEST(GroundVehicleCmd, TargetsValidateRefuseWhileHaltedAndCountWaypoints) {
  ReadyRover r;
  EXPECT_EQ(sendBytes(r.rover, RoverOpcode::SET_SEQ_STATE, {3u, 2u}), 0u);
  EXPECT_EQ(sendBytes(r.rover, RoverOpcode::SET_TARGET_REL, targetBytes(1.52F, 0.0F)), 0u);
  EXPECT_EQ(sendBytes(r.rover, RoverOpcode::SET_TARGET_ABS, targetBytes(10.0F, -4.0F)), 0u);
  const auto& s = r.rover.vehicleState();
  EXPECT_EQ(s.target_kind, 2u);
  EXPECT_EQ(s.target_seq, 2u);
  EXPECT_EQ(s.active_waypoint, 2u);
  EXPECT_FLOAT_EQ(s.target_a_m, 10.0F);

  EXPECT_NE(sendBytes(r.rover, RoverOpcode::SET_TARGET_REL, targetBytes(5000.0F, 0.0F)), 0u)
      << "out of bounds";
  EXPECT_EQ(s.target_seq, 2u) << "rejected whole";
  EXPECT_EQ(sendBytes(r.rover, RoverOpcode::HALT, {}), 0u);
  EXPECT_EQ(sendBytes(r.rover, RoverOpcode::SET_TARGET_REL, targetBytes(1.0F, 0.0F)),
            static_cast<std::uint8_t>(system_core::system_component::CommandResult::EXEC_FAILED));
  const auto* f = r.frame();
  EXPECT_EQ(f[fb::FB_LAST_CMD_RESULT], static_cast<std::uint8_t>(CmdResultCode::NACK_EXEC_FAILED));
  EXPECT_EQ(f[fb::FB_SEQ_STATE], 3u);
  EXPECT_EQ(f[fb::FB_WAYPOINT_TOTAL], 2u);
  EXPECT_EQ(f[fb::FB_ACTIVE_WAYPOINT], 2u);
  EXPECT_EQ(f[fb::FB_CONTROLLER_MODE], appsim::ground_vehicle::kFrameModeHalted);
}

TEST(GroundVehicleCmd, LedCommandsAreBoundedAndStamped) {
  ReadyRover r;
  EXPECT_EQ(sendBytes(r.rover, RoverOpcode::SET_LED, {1u, 2u, 5u}), 0u);
  EXPECT_EQ(sendBytes(r.rover, RoverOpcode::SET_LED, {2u, 1u, 0u}), 0u);
  EXPECT_NE(sendBytes(r.rover, RoverOpcode::SET_LED, {3u, 1u, 1u}), 0u) << "lamp";
  EXPECT_NE(sendBytes(r.rover, RoverOpcode::SET_LED, {1u, 6u, 1u}), 0u) << "colour";
  EXPECT_NE(sendBytes(r.rover, RoverOpcode::SET_LED, {1u, 1u, 6u}), 0u) << "rate";
  const auto* f = r.frame();
  EXPECT_EQ(f[fb::FB_LED1_COLOUR], 2u);
  EXPECT_EQ(f[fb::FB_LED1_RATE], 5u);
  EXPECT_EQ(f[fb::FB_LED2_COLOUR], 1u);
  EXPECT_EQ(f[fb::FB_LED2_RATE], 0u);
  EXPECT_EQ(f[fb::FB_LAST_CMD_RESULT],
            static_cast<std::uint8_t>(CmdResultCode::NACK_INVALID_ARGUMENT));
  EXPECT_EQ(f[fb::FB_LAST_CMD_OPCODE_LO], 0x06u);
}

/* ----------------------------- LEDs + step rate ----------------------------- */

TEST(GroundVehicleLed, StrobeBitFollowsTheRateCodeAt100Hz) {
  ReadyRover r;
  r.rover.tunables().get().step_hz = 100u;
  ASSERT_EQ(sendBytes(r.rover, RoverOpcode::SET_LED, {1u, 2u, 5u}), 0u); // green, 10 Hz
  ASSERT_EQ(sendBytes(r.rover, RoverOpcode::SET_LED, {2u, 1u, 0u}), 0u); // red, steady
  // 10 Hz at 100 steps/s: period 10 steps, 5 on then 5 off, repeating.
  std::vector<int> lamp1;
  for (int i = 0; i < 30; ++i) {
    (void)r.rover.vehicleStep();
    const auto* f = r.rover.frameBytes();
    lamp1.push_back((f[fb::FB_LED_BITS] & 0x01u) != 0u ? 1 : 0);
    EXPECT_EQ((f[fb::FB_LED_BITS] & 0x02u) != 0u, true) << "steady lamp stays on";
  }
  int on = 0, transitions = 0;
  for (std::size_t i = 0; i < lamp1.size(); ++i) {
    on += lamp1[i];
    if (i > 0 && lamp1[i] != lamp1[i - 1]) {
      ++transitions;
    }
  }
  EXPECT_EQ(on, 15) << "half the steps on over three periods";
  EXPECT_EQ(transitions, 5) << "toggles every 5 steps";
  for (std::size_t i = 0; i + 5 < lamp1.size(); i += 10) {
    EXPECT_EQ(lamp1[i], 1) << "period starts on";
    EXPECT_EQ(lamp1[i + 5], 0) << "half period off";
  }

  ASSERT_EQ(sendBytes(r.rover, RoverOpcode::SET_LED, {1u, 0u, 5u}), 0u); // colour off wins
  (void)r.rover.vehicleStep();
  EXPECT_EQ(r.rover.frameBytes()[fb::FB_LED_BITS] & 0x01u, 0u);
}

TEST(GroundVehicleLed, HalfHertzAtTenHzStepsIsTenOnTenOff) {
  ReadyRover r;                                                          // step_hz default 10
  ASSERT_EQ(sendBytes(r.rover, RoverOpcode::SET_LED, {1u, 3u, 1u}), 0u); // blue, 0.5 Hz
  int on = 0;
  for (int i = 0; i < 20; ++i) {
    (void)r.rover.vehicleStep();
    on += (r.rover.frameBytes()[fb::FB_LED_BITS] & 0x01u) != 0u ? 1 : 0;
  }
  EXPECT_EQ(on, 10);
}

TEST(GroundVehicle, StepRateScalesTheIntegrationAndTheTimestampGrid) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);
  GroundVehicle slow, fast;
  configureRover(slow);
  configureRover(fast);
  slow.tunables().get().step_hz = 10u;
  fast.tunables().get().step_hz = 100u;
  slow.setBody(&earth);
  fast.setBody(&earth);
  for (int i = 0; i < 20; ++i) {
    (void)slow.vehicleStep(); // 2 s
  }
  for (int i = 0; i < 200; ++i) {
    (void)fast.vehicleStep(); // 2 s
  }
  // Same elapsed time: first-order speed approach and heading agree to
  // the integration-step difference.
  EXPECT_NEAR(fast.telemetry().speed_m_s, slow.telemetry().speed_m_s, 0.05);
  EXPECT_NEAR(fast.telemetry().heading_deg, slow.telemetry().heading_deg, 1e-6);
  // Timestamp grid follows the step rate.
  const std::uint64_t T1 = fast.telemetry().timestamp_ns;
  (void)fast.vehicleStep();
  EXPECT_EQ(fast.telemetry().timestamp_ns - T1, 10000000u);
}
