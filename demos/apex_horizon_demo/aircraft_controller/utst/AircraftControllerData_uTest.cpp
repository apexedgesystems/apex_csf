/**
 * @file AircraftControllerData_uTest.cpp
 * @brief Default-value + size sanity tests for AircraftControllerTunables /
 *        AircraftControllerState / AircraftControllerOutput.
 *
 * Output is *not* wire-format — it stays inside apex. Sizes are still
 * pinned so silent struct drift trips a test failure.
 */

#include "demos/apex_horizon_demo/aircraft_controller/inc/AircraftController.hpp"
#include "demos/apex_horizon_demo/aircraft_controller/inc/AircraftControllerData.hpp"

#include <gtest/gtest.h>

using appsim::aircraft_controller::AircraftController;
using appsim::aircraft_controller::AircraftControllerOutput;
using appsim::aircraft_controller::AircraftControllerState;
using appsim::aircraft_controller::AircraftControllerTunables;

/* ----------------------------- Sizes ----------------------------- */

TEST(AircraftControllerDataTest, TunablesFitInTprmPage) {
  EXPECT_LE(sizeof(AircraftControllerTunables), 256u);
}

TEST(AircraftControllerDataTest, StateSize) {
  // 8 (tick) + 8 (elapsed) + 1 (init) + 15 (reserved) = 32
  EXPECT_EQ(sizeof(AircraftControllerState), 32u);
}

/* ----------------------------- Tunable defaults ----------------------------- */

TEST(AircraftControllerDataTest, TunableDefaultsBracket) {
  AircraftControllerTunables p;

  // Setpoints: cruise altitude / airspeed / heading default to a sane
  // open-loop demo trajectory.
  EXPECT_EQ(p.target_altitude_m, 8000.0);
  EXPECT_EQ(p.target_airspeed_m_s, 240.0);
  EXPECT_EQ(p.target_heading_deg, 45.0);
  EXPECT_GT(p.trim_throttle, 0.0);
  EXPECT_LE(p.trim_throttle, 1.0);

  // Pitch loop: positive Kp + reasonable elevator clamp.
  EXPECT_GT(p.pitch_Kp, 0.0);
  EXPECT_GT(p.elevator_limit_rad, 0.0);
  EXPECT_LT(p.elevator_limit_rad, 1.0); // < ~57°

  // Altitude loop: small Kp (h-error in meters * 0.0008 ≈ rad, sensible).
  EXPECT_GT(p.alt_Kp, 0.0);
  EXPECT_LT(p.alt_Kp, 0.01);
  EXPECT_GT(p.pitch_ref_limit_rad, 0.0);

  // Roll loop: positive Kp + aileron clamp.
  EXPECT_GT(p.roll_Kp, 0.0);
  EXPECT_GT(p.aileron_limit_rad, 0.0);
  EXPECT_LT(p.aileron_limit_rad, 1.0);

  // Heading loop: bank limit must be < π/2 (else loop is ill-defined).
  EXPECT_GT(p.bank_limit_rad, 0.0);
  EXPECT_LT(p.bank_limit_rad, 1.5);

  // Yaw damper: positive proportional gain.
  EXPECT_GT(p.yaw_Kr, 0.0);

  // Default mode is pass-through (open-loop fallback).
  EXPECT_EQ(p.enable_mode, 0u);
}

TEST(AircraftControllerDataTest, OutputDefaultsAreZero) {
  AircraftControllerOutput o;
  EXPECT_EQ(o.elevator_rad, 0.0);
  EXPECT_EQ(o.aileron_rad, 0.0);
  EXPECT_EQ(o.rudder_rad, 0.0);
  EXPECT_EQ(o.throttle, 0.0);
  EXPECT_EQ(o.tick, 0u);
  EXPECT_EQ(o.mode, 0u);
}

/* ----------------------------- SW model smoke ----------------------------- */

/** @test The full AircraftController SwModelBase compiles + constructs. */
TEST(AircraftControllerSmokeTest, ConstructsAndExposesIdentity) {
  AircraftController c;
  EXPECT_EQ(c.componentId(), 225u);
  EXPECT_STREQ(c.componentName(), "AircraftController");
  EXPECT_STREQ(c.label(), "AIRCRAFT_CTL");
  EXPECT_EQ(c.aircraft(), nullptr);
}

/** @test In pass-through mode (default), controllerStep emits zero
 *        controls and trim throttle without needing an Aircraft attached.
 */
TEST(AircraftControllerSmokeTest, PassThroughModeEmitsZeros) {
  AircraftController c;
  // Drive one tick with default tunables (enable_mode = 0, no aircraft).
  EXPECT_EQ(c.controllerStep(), 0u);
  const auto& out = c.controllerOutput();
  EXPECT_EQ(out.elevator_rad, 0.0);
  EXPECT_EQ(out.aileron_rad, 0.0);
  EXPECT_EQ(out.rudder_rad, 0.0);
  EXPECT_EQ(out.throttle, c.tunables().get().trim_throttle);
  EXPECT_EQ(out.mode, 0u);
}
