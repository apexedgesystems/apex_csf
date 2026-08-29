/**
 * @file AircraftControllerClosedLoop_uTest.cpp
 * @brief Closed-loop integration tests: AircraftController ↔ Aircraft ↔ CelestialBody.
 *
 * These tests exercise the full feedback path that theb–f
 * AircraftController is responsible for. They exist specifically to
 * regress the three nested bugs found on 2026-05-16 in the yaw
 * damper path (a stale scalar-model stub once hard-coded r=0; the
 * §8.10 sign error; unit test that locked the wrong sign) and the
 * SET_GUST_ALLEVIATION_ENABLE wiring.
 *
 * Fixture: minimal analytic-Earth CelestialBody (no data files) + a
 * default transport Aircraft + a default AircraftController in
 * closed-loop mode. Tick the controller at 25 Hz and the aircraft at
 * 100 Hz (four integrator steps per control update) to match the
 * apex_horizon_demo scheduler.
 *
 * Coverage:
 *   - ClosedLoopCruiseDoesntDiverge:
 *       30 s of trim cruise stays bounded. With the pre-2026-05-16
 *       yaw damper sign error, ψ_err diverged to ±π in ~2 min and
 *       altitude/speed walked off by km/m·s. This test would have
 *       failed on the old code.
 *
 *   - YawDamperOutputUsesAircraftBodyYawRate:
 *       Catches the stale "scalar model" comment that hard-coded
 *       yaw_rate_rad_s = 0.0. Verifies that when the aircraft exhibits
 *       a non-trivial body yaw rate r, the controller emits a rudder
 *       command in the washed-out r-feedback sign convention (sign matches r —
 *       sign-correct via Cn_δr<0 → opposing yaw moment).
 *
 *   - GustAlleviationToggleAffectsElevator:
 *       Regression for the command-stub failure mode. With turbulence ON and a
 *       non-zero vertical gust developed by the Aircraft, the
 *       controller's elevator command must differ when
 *       SET_GUST_ALLEVIATION_ENABLE is OFF vs ON.
 *
 *   - HeadingStepCommandsRightBank:
 *       Golden-path: a +30° heading setpoint perturbation drives the
 *       outer heading loop to command positive φ_ref (right bank);
 *       the inner roll loop in turn commands positive δa. Verifies
 *       the cascade is wired correctly.
 */

#include "demos/apex_horizon_demo/aircraft_controller/inc/AircraftController.hpp"
#include "demos/apex_horizon_demo/aircraft_controller/inc/AircraftControllerData.hpp"
#include "demos/apex_horizon_demo/aircraft/inc/Aircraft.hpp"
#include "demos/apex_horizon_demo/aircraft/inc/AircraftCommand.hpp"

#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBodyData.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

using appsim::aircraft::Aircraft;
using appsim::aircraft::AircraftCmdSetEnable;
using appsim::aircraft::AircraftOpcode;
using appsim::aircraft_controller::AircraftController;
using sim::environment::AtmosphereFidelity;
using sim::environment::Body;
using sim::environment::GravityFidelity;
using sim::environment::TerrainFidelity;
using sim::environment::celestial_body::CelestialBody;
using sim::environment::celestial_body::CelestialBodyTunables;

namespace {

/* ----------------------------- Fixture helpers ----------------------------- */

/// Analytic Earth (J2 gravity, ellipsoid terrain, exponential atmosphere).
/// No data files — pure parameter math, suitable for unit-test use.
CelestialBodyTunables analyticEarth() {
  CelestialBodyTunables t{};
  t.body = Body::EARTH;
  t.gravity_fidelity = GravityFidelity::J2;
  t.terrain_fidelity = TerrainFidelity::ELLIPSOID;
  t.atmosphere_fidelity = AtmosphereFidelity::EXPONENTIAL;
  return t;
}

/// Build a controller whose setpoints match the aircraft's init pose so
/// the loops start at zero error (trim-cruise baseline).
void configureTrimCruise(AircraftController& ctl, Aircraft& a) {
  auto& p = ctl.tunables().get();
  const auto& at = a.tunables().get();
  p.target_altitude_m = at.init_alt_m;
  p.target_airspeed_m_s = at.init_speed_m_s;
  p.target_heading_deg = at.init_heading_deg;
  p.enable_mode = 1; // closed loop
}

/// Drive one controller tick + four aircraft sub-steps (matches the
/// 25 Hz controller / 100 Hz plant cadence in apex_horizon_demo).
void tickOnce(AircraftController& ctl, Aircraft& a) {
  ctl.controllerStep();
  a.aircraftStep();
  a.aircraftStep();
  a.aircraftStep();
  a.aircraftStep();
}

/// Tick N controller ticks (i.e. N/25 simulated seconds).
void tickN(AircraftController& ctl, Aircraft& a, int n) {
  for (int i = 0; i < n; ++i)
    tickOnce(ctl, a);
}

/// Send SET_GUST_ALLEVIATION_ENABLE to the aircraft (0=off, 1=on).
void setGustAlleviation(Aircraft& a, std::uint8_t enabled) {
  std::uint8_t storage = enabled;
  apex::compat::rospan<std::uint8_t> payload(&storage, 1);
  std::vector<std::uint8_t> resp;
  (void)a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_GUST_ALLEVIATION_ENABLE),
                        payload, resp);
}

/// Send SET_TURBULENCE_ENABLE to the aircraft (0=off, 1=on).
void setTurbulence(Aircraft& a, std::uint8_t enabled) {
  std::uint8_t storage = enabled;
  apex::compat::rospan<std::uint8_t> payload(&storage, 1);
  std::vector<std::uint8_t> resp;
  (void)a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::SET_TURBULENCE_ENABLE), payload,
                        resp);
}

} // namespace

/* ----------------------------- Tests ----------------------------- */

/**
 * @test 30 s of trim cruise stays bounded.
 *
 * Regresses the pre-2026-05-16 yaw damper sign error. With δr = -K_r·r
 * (wrong sign) combined with Cn_δr<0, positive yaw rate produced a
 * positive (amplifying) yaw moment. ψ_err diverged to ±π in ~2 min
 * and the heading loop slammed bank to its limit; altitude and speed
 * walked off in the resulting uncoordinated turn.
 *
 * With δr = +K_r·r_hp (washed-out yaw-rate feedback) the system stays in trim.
 */
TEST(AircraftControllerClosedLoopTest, ClosedLoopCruiseDoesntDiverge) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);

  Aircraft aircraft;
  aircraft.setBody(&earth);
  setTurbulence(aircraft, 0); // deterministic: no Dryden

  AircraftController controller;
  controller.setAircraft(&aircraft);
  configureTrimCruise(controller, aircraft);
  aircraft.setControllerOutput(&controller.controllerOutput());

  // 30 s = 750 controller ticks.
  tickN(controller, aircraft, 750);

  const auto& out = controller.controllerOutput();
  EXPECT_LT(std::abs(out.altitude_error_m), 200.0)
      << "altitude walked off — yaw damper sign or wiring regressed";
  EXPECT_LT(std::abs(out.airspeed_error_m_s), 20.0) << "airspeed walked off — controller diverged";
  EXPECT_LT(std::abs(out.heading_error_rad), 0.1)
      << "heading walked off — Dutch roll uncontrolled (pre-2026-05-16 bug)";
}

/**
 * @test Rudder command derives from the aircraft's body yaw rate.
 *
 * Regresses the stale `yaw_rate_rad_s = 0.0` hard-coding in
 * AircraftController.hpp. After a heading-setpoint perturbation drives
 * the aircraft into a coordinated turn, the body yaw rate r becomes
 * non-trivial — the washout filter still passes the *transient* portion
 * of r, so the controller must emit a non-zero rudder at some point
 * during the transient. If the controller is feeding zero to the
 * damper (the old bug), δr stays at 0 throughout.
 */
TEST(AircraftControllerClosedLoopTest, YawDamperOutputUsesAircraftBodyYawRate) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);

  Aircraft aircraft;
  aircraft.setBody(&earth);
  setTurbulence(aircraft, 0);

  AircraftController controller;
  controller.setAircraft(&aircraft);
  configureTrimCruise(controller, aircraft);
  // 30° heading-step perturbation excites the turn (body yaw rate r).
  controller.tunables().get().target_heading_deg =
      aircraft.tunables().get().init_heading_deg + 30.0;
  aircraft.setControllerOutput(&controller.controllerOutput());

  double max_abs_rudder = 0.0;
  for (int i = 0; i < 250; ++i) { // 10 s
    tickOnce(controller, aircraft);
    const double dr = controller.controllerOutput().rudder_rad;
    if (std::abs(dr) > max_abs_rudder)
      max_abs_rudder = std::abs(dr);
  }
  EXPECT_GT(max_abs_rudder, 1e-4) << "rudder never moved — controller is not feeding body yaw rate "
                                     "to the YawDamper (regression of the 'scalar model' bug)";
}

/**
 * @test SET_GUST_ALLEVIATION_ENABLE actually toggles the controller's
 *       elevator feedforward contribution.
 *
 * Regresses the command-stub failure mode. Run the aircraft long enough for the
 * Dryden gust state to develop a non-zero w_g, snapshot the elevator
 * command, then disable gust alleviation and snapshot again at the
 * matching point. The two elevators must differ when the gust
 * contribution is non-trivial.
 */
TEST(AircraftControllerClosedLoopTest, GustAlleviationToggleAffectsElevator) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);

  Aircraft aircraft;
  aircraft.setBody(&earth);
  setTurbulence(aircraft, 1); // need real gusts for the test

  AircraftController controller;
  controller.setAircraft(&aircraft);
  configureTrimCruise(controller, aircraft);
  controller.tunables().get().gust_authority_pct = 1.0; // full feedforward
  aircraft.setControllerOutput(&controller.controllerOutput());

  // Run 5 s to develop a non-zero Dryden state, then sample with GA ON.
  tickN(controller, aircraft, 125);
  // Wait for a tick whose gust is meaningfully non-zero.
  double w_g = 0.0;
  for (int i = 0; i < 250 && std::abs(w_g) < 0.5; ++i) {
    tickOnce(controller, aircraft);
    w_g = aircraft.latestVerticalGust_m_s();
  }
  ASSERT_GT(std::abs(w_g), 0.5)
      << "Dryden never produced a meaningful gust; can't measure GA effect";

  const double elev_with_ga = controller.controllerOutput().elevator_rad;

  // Disable GA via the command path, tick once with the SAME w_g
  // present (Aircraft caches it across ticks), and re-sample.
  setGustAlleviation(aircraft, 0);
  ASSERT_FALSE(aircraft.isGustAlleviationEnabled());

  controller.controllerStep(); // re-run the controller without stepping
                               // the plant so w_g is identical
  const double elev_without_ga = controller.controllerOutput().elevator_rad;

  EXPECT_NE(elev_with_ga, elev_without_ga)
      << "SET_GUST_ALLEVIATION_ENABLE OFF did not change the elevator — "
         "command stub regressed (controller is still adding δe_gust)";
}

/**
 * @test +30° heading-setpoint step commands positive bank reference
 *       and positive aileron (right turn).
 *
 * Golden-path verification of the heading → roll → aileron cascade.
 */
TEST(AircraftControllerClosedLoopTest, HeadingStepCommandsRightBank) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);

  Aircraft aircraft;
  aircraft.setBody(&earth);
  setTurbulence(aircraft, 0);

  AircraftController controller;
  controller.setAircraft(&aircraft);
  configureTrimCruise(controller, aircraft);
  controller.tunables().get().target_heading_deg =
      aircraft.tunables().get().init_heading_deg + 30.0;
  aircraft.setControllerOutput(&controller.controllerOutput());

  // First few ticks: heading error is large + positive → bank loop
  // should saturate at the +bank_limit; aileron should be commanded
  // positive (right roll).
  tickN(controller, aircraft, 10);
  const auto& out = controller.controllerOutput();
  EXPECT_GT(out.bank_ref_rad, 0.0) << "positive heading error did not command right bank";
  EXPECT_GT(out.aileron_rad, 0.0) << "positive bank reference did not command right aileron";
}

/* ----------------------------- Mode accommodation ----------------------------- */

namespace {

/// Runs a coupled aircraft+controller pair (100 Hz / 25 Hz) from trim,
/// with turbulence commanded off and the controller restricted to
/// `loop_mask`. Fires a rudder doublet, then returns the peak |r| in
/// two consecutive post-doublet windows (one expected Dutch roll
/// period each) so callers can compare cycle-to-cycle amplitude.
struct DutchRollDecay {
  double peak_cycle1;
  double peak_cycle2;
};

DutchRollDecay measureDutchRollDecay(std::uint8_t loop_mask) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  EXPECT_EQ(earth.init(), 0u);

  Aircraft ac;
  ac.setBody(&earth);
  EXPECT_EQ(ac.init(), 0u);

  AircraftController ctl;
  ctl.setAircraft(&ac);
  ctl.tunables().get().enable_mode = 1u;
  ctl.setLoopEnableMask(loop_mask);
  EXPECT_EQ(ctl.init(), 0u);
  ac.setControllerOutput(&ctl.controllerOutput());

  // Command turbulence OFF so the measurement sees only the doublet
  // response (also exercises the toggle path).
  const std::uint8_t OFF = 0u;
  apex::compat::rospan<std::uint8_t> payload(&OFF, 1);
  std::vector<std::uint8_t> response;
  EXPECT_EQ(ac.handleCommand(0x0100u, payload, response), 0u);

  // Settle at trim, then excite.
  for (int i = 0; i < 200; ++i) {
    if (i % 4 == 0) {
      (void)ctl.controllerStep();
    }
    (void)ac.aircraftStep();
  }
  ac.startExcitation(Aircraft::ExciteMode::RUDDER_DOUBLET);

  // One expected Dutch roll period ~7 s; sample |r| over two windows
  // following the 1 s doublet.
  constexpr double DT = 1.0 / 100.0;
  constexpr double T_WINDOW_S = 7.0;
  DutchRollDecay d{0.0, 0.0};
  const int n = static_cast<int>((1.0 + 2.0 * T_WINDOW_S) / DT);
  for (int i = 0; i < n; ++i) {
    if (i % 4 == 0) {
      (void)ctl.controllerStep();
    }
    (void)ac.aircraftStep();
    const double t = (i + 1) * DT;
    const double ar = std::fabs(ac.telemetry().r_rad_s);
    if (t >= 1.0 && t < 1.0 + T_WINDOW_S && ar > d.peak_cycle1) {
      d.peak_cycle1 = ar;
    }
    if (t >= 1.0 + T_WINDOW_S && ar > d.peak_cycle2) {
      d.peak_cycle2 = ar;
    }
  }
  return d;
}

} // namespace

/** @test The yaw damper measurably raises Dutch roll damping: with all
 *  loops off the doublet response persists cycle to cycle; with only
 *  the yaw damper enabled the second-cycle amplitude drops well below
 *  the undamped case. This is the controller-accommodation physics the
 *  paired demo shows on camera. */
TEST(AircraftControllerModes, YawDamperRaisesDutchRollDamping) {
  const auto bare = measureDutchRollDecay(0u);
  const auto damped = measureDutchRollDecay(AircraftController::LOOP_YAW_DAMPER);

  ASSERT_GT(bare.peak_cycle1, 1e-4) << "doublet failed to excite the bare airframe";
  ASSERT_GT(damped.peak_cycle1, 1e-5) << "doublet failed to excite the damped airframe";

  // Bare airframe: lightly damped — the mode survives into cycle 2.
  const double bare_ratio = bare.peak_cycle2 / bare.peak_cycle1;
  EXPECT_GT(bare_ratio, 0.4) << "bare-airframe Dutch roll decayed too fast to demo";

  // Yaw damper on: cycle-2 amplitude must drop well below the bare case.
  const double damped_ratio = damped.peak_cycle2 / damped.peak_cycle1;
  EXPECT_LT(damped_ratio, 0.6 * bare_ratio)
      << "yaw damper did not measurably raise Dutch roll damping: bare ratio = " << bare_ratio
      << ", damped ratio = " << damped_ratio;
}

/* ---------------------- Boot-to-trim golden ---------------------- */

/** @test From the demo's exact boot initial conditions (level attitude
 *  at target altitude/speed, heading 90 degrees off its target, pitch
 *  at zero rather than trim alpha), the coupled closed-loop sim must
 *  REACH trim and hold it: altitude and speed captured, wings level,
 *  heading on target, elevator settled near its trim deflection.
 *  Turbulence is commanded off for determinism. This is the demo's
 *  "did we achieve trim" contract as a regression. */
TEST(AircraftControllerModes, BootConditionsReachTrim) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  Aircraft ac;
  ac.setBody(&earth);
  // The demo's boot-IC STRUCTURE at a reference point this test's
  // world can sustain: the data-file-free EXPONENTIAL atmosphere is
  // ~20% thinner than the demo's USSA76 table at the demo's 12.2 km
  // condition, where density-scaled max thrust cannot balance drag
  // (the demo's own point holds live on the real table, with ~1%
  // thrust margin). 8 km / 220 m/s has comfortable margin here.
  ac.tunables().get().init_alt_m = 8000.0;
  ac.tunables().get().init_speed_m_s = 220.0;
  ASSERT_EQ(ac.init(), 0u);

  AircraftController ctl;
  ctl.setAircraft(&ac);
  ctl.tunables().get().enable_mode = 1u;
  ctl.tunables().get().target_altitude_m = 8000.0;
  ctl.tunables().get().target_airspeed_m_s = 220.0;
  // Mirror the demo's one off-trim commanded value: the heading
  // target, 90 degrees from the boot heading (code defaults target
  // the boot heading — trim from the first tick).
  ctl.tunables().get().target_heading_deg = 135.0;
  ASSERT_EQ(ctl.init(), 0u);
  ac.setControllerOutput(&ctl.controllerOutput());

  const std::uint8_t OFF = 0u;
  apex::compat::rospan<std::uint8_t> payload(&OFF, 1);
  std::vector<std::uint8_t> response;
  ASSERT_EQ(ac.handleCommand(0x0100u, payload, response), 0u); // turbulence off

  // 240 s of sim at the production cadence (100 Hz plant, 25 Hz ctl):
  // the capture takes ~90 s; the rest must be quiet trim.
  for (int i = 0; i < 24000; ++i) {
    if (i % 4 == 0) {
      (void)ctl.controllerStep();
    }
    ASSERT_EQ(ac.aircraftStep(), 0u);
  }

  const auto& TLM = ac.telemetry();
  const auto& P = ac.tunables().get();
  EXPECT_NEAR(TLM.pos_alt_m, P.init_alt_m, 15.0);       // altitude held
  EXPECT_NEAR(TLM.airspeed_m_s, P.init_speed_m_s, 1.5); // speed held
  EXPECT_LT(std::fabs(TLM.roll_deg), 2.0);              // wings level
  {
    double dpsi = 135.0 - TLM.heading_deg; // ctl default target
    while (dpsi > 180.0)
      dpsi -= 360.0;
    while (dpsi < -180.0)
      dpsi += 360.0;
    EXPECT_LT(std::fabs(dpsi), 1.5);
  }
  EXPECT_LT(std::fabs(TLM.elevator_rad), 0.05); // settled near trim
}

/* -------------------- ACFT/2 command surface (0x0103/0x0104) -------------------- */

namespace {

/// Send a 1-byte command; returns the CommandResult code.
std::uint8_t sendByteCmd(Aircraft& a, std::uint16_t opcode, std::uint8_t value,
                         std::vector<std::uint8_t>* respOut = nullptr) {
  std::uint8_t storage = value;
  apex::compat::rospan<std::uint8_t> payload(&storage, 1);
  std::vector<std::uint8_t> resp;
  const std::uint8_t RC = a.handleCommand(opcode, payload, resp);
  if (respOut != nullptr) {
    *respOut = resp;
  }
  return RC;
}

std::uint8_t queryLoopMask(Aircraft& a) {
  std::vector<std::uint8_t> resp;
  (void)sendByteCmd(a, 0x0102u, 0u, &resp); // GET_COMMAND_STATE ignores payload
  return resp.at(2);                        // loop_enable_mask offset in the snapshot
}

} // namespace

TEST(AircraftCommandSurface, LoopMaskCommandAdoptedNextControllerTick) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);
  Aircraft aircraft;
  aircraft.setBody(&earth);
  setTurbulence(aircraft, 0);
  AircraftController controller;
  controller.setAircraft(&aircraft);
  configureTrimCruise(controller, aircraft);
  aircraft.setControllerOutput(&controller.controllerOutput());

  // Boot truth: all loops, from both sides' defaults.
  EXPECT_EQ(queryLoopMask(aircraft), 0x3Fu);

  // Drop the yaw damper (bit 5): accepted, adopted on the next tick.
  EXPECT_EQ(sendByteCmd(aircraft, 0x0103u, 0x1Fu), 0u);
  tickOnce(controller, aircraft);
  EXPECT_EQ(controller.loopEnableMask(), 0x1Fu);
  EXPECT_EQ(queryLoopMask(aircraft), 0x1Fu);

  // Unknown bits reject whole; the running mask is untouched.
  EXPECT_NE(sendByteCmd(aircraft, 0x0103u, 0x40u), 0u);
  tickOnce(controller, aircraft);
  EXPECT_EQ(controller.loopEnableMask(), 0x1Fu);

  // Restore all loops.
  EXPECT_EQ(sendByteCmd(aircraft, 0x0103u, 0x3Fu), 0u);
  tickOnce(controller, aircraft);
  EXPECT_EQ(controller.loopEnableMask(), 0x3Fu);
}

TEST(AircraftCommandSurface, ExciteArmsOnceAndReportsTruth) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);
  Aircraft aircraft;
  aircraft.setBody(&earth);
  setTurbulence(aircraft, 0);
  AircraftController controller;
  controller.setAircraft(&aircraft);
  configureTrimCruise(controller, aircraft);
  aircraft.setControllerOutput(&controller.controllerOutput());

  // Unknown mode ids reject.
  EXPECT_NE(sendByteCmd(aircraft, 0x0104u, 0u), 0u);
  EXPECT_NE(sendByteCmd(aircraft, 0x0104u, 5u), 0u);

  // Arm the rudder doublet; snapshot reports it armed.
  EXPECT_EQ(sendByteCmd(aircraft, 0x0104u, 1u), 0u);
  {
    std::vector<std::uint8_t> resp;
    (void)sendByteCmd(aircraft, 0x0102u, 0u, &resp);
    EXPECT_EQ(resp.at(3), 1u); // active_excite_mode
  }

  // A second injection while armed is rejected -- the trace window
  // stays clean -- and the armed mode is unchanged.
  EXPECT_NE(sendByteCmd(aircraft, 0x0104u, 3u), 0u);
  EXPECT_EQ(static_cast<std::uint8_t>(aircraft.activeExcitation()), 1u);

  // Play the doublet out (1.0 s = 25 controller ticks at 100 Hz plant);
  // it self-clears and the snapshot returns to none, after which a new
  // excitation arms cleanly.
  tickN(controller, aircraft, 30);
  {
    std::vector<std::uint8_t> resp;
    (void)sendByteCmd(aircraft, 0x0102u, 0u, &resp);
    EXPECT_EQ(resp.at(3), 0u);
  }
  EXPECT_EQ(sendByteCmd(aircraft, 0x0104u, 3u), 0u);
}

TEST(AircraftCommandSurface, WireExcitationCapturesModeTrace) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);
  Aircraft aircraft;
  aircraft.setBody(&earth);
  setTurbulence(aircraft, 0);
  AircraftController controller;
  controller.setAircraft(&aircraft);
  configureTrimCruise(controller, aircraft);
  aircraft.setControllerOutput(&controller.controllerOutput());

  // Wire-armed excitation opens the trace window: 20 Hz capture means
  // one sample per 5 plant ticks. 2 s of flight = 40 samples, but the
  // bounded buffer holds 64 without a telemetry drain, so all pend.
  EXPECT_EQ(aircraft.modeTracePending(), 0u);
  EXPECT_EQ(sendByteCmd(aircraft, 0x0104u, 1u), 0u);
  tickN(controller, aircraft, 50 / 4); // ~0.5 s
  const std::size_t AFTER_HALF_S = aircraft.modeTracePending();
  EXPECT_GE(AFTER_HALF_S, 8u);
  EXPECT_LE(AFTER_HALF_S, 14u);

  // Suite-armed excitation (direct startExcitation) does NOT trace:
  // the trace is a wire-window instrument, not a physics side effect.
  Aircraft bare;
  bare.setBody(&earth);
  setTurbulence(bare, 0);
  bare.startExcitation(Aircraft::ExciteMode::RUDDER_DOUBLET);
  for (int i = 0; i < 100; ++i) {
    (void)bare.aircraftStep();
  }
  EXPECT_EQ(bare.modeTracePending(), 0u);
}

/**
 * @test Recovery-envelope characterization: the orchestrated restore
 * actually recovers.
 *
 * The mode set-pieces drop loops, let a mode develop, then restore
 * LOOP_ALL -- either on schedule or early via the boundary
 * watchpoint. This pins the physics claim underneath: from a phugoid
 * developed for a full half-exchange with the longitudinal loops
 * dropped, restoring the loops recaptures cruise. The phugoid is the
 * binding case (recovery authority is thrust-margin-limited); the
 * lateral modes recover with large margin by comparison.
 *
 * Runs at the closed-loop suite's reference point. The demo-altitude
 * boundary values (thinner margin) are measured on the demo itself
 * and live in its action-engine TPRM, not in code.
 */
TEST(AircraftCommandSurface, PhugoidDevelopedThenRestoredRecapturesCruise) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);
  Aircraft aircraft;
  aircraft.setBody(&earth);
  setTurbulence(aircraft, 0);
  AircraftController controller;
  controller.setAircraft(&aircraft);
  configureTrimCruise(controller, aircraft);
  aircraft.setControllerOutput(&controller.controllerOutput());

  // Settle at trim first.
  tickN(controller, aircraft, 750); // 30 s
  const auto& out = controller.controllerOutput();
  ASSERT_LT(std::abs(out.altitude_error_m), 50.0) << "no trim before the experiment";

  // Set-piece shape: drop PITCH|ALT|SPEED (keep lateral), arm the
  // phugoid excitation, let the exchange develop for 60 s (~half a
  // period -- maximum energy displaced from trim).
  ASSERT_EQ(sendByteCmd(aircraft, 0x0103u, 0x38u), 0u); // lateral only
  ASSERT_EQ(sendByteCmd(aircraft, 0x0104u, 4u), 0u);    // SPEED_OFFSET
  tickN(controller, aircraft, 1500);                    // 60 s

  const double ALT_ERR_DEVELOPED = std::abs(out.altitude_error_m);
  const double SPD_ERR_DEVELOPED = std::abs(out.airspeed_error_m_s);
  // The mode must actually have displaced the state, or this test
  // proves nothing.
  ASSERT_GT(ALT_ERR_DEVELOPED + SPD_ERR_DEVELOPED * 10.0, 30.0)
      << "phugoid failed to develop: alt_err=" << ALT_ERR_DEVELOPED
      << " spd_err=" << SPD_ERR_DEVELOPED;

  // The orchestrated restore. Recovery from a deep excursion is
  // total-energy-limited and two-phase: the loops fly the aircraft
  // back to altitude at saturated throttle first (airspeed pays for
  // the climb), then rebuild speed once the climb ends. Assert each
  // phase on its own timescale.
  ASSERT_EQ(sendByteCmd(aircraft, 0x0103u, 0x3Fu), 0u);

  tickN(controller, aircraft, 3000); // 120 s: altitude capture phase
  EXPECT_LT(std::abs(out.altitude_error_m), 30.0)
      << "altitude not recaptured from developed phugoid (was " << ALT_ERR_DEVELOPED << " m off)";
  const double SPD_ERR_AFTER_CLIMB = std::abs(out.airspeed_error_m_s);

  tickN(controller, aircraft, 3000); // +120 s: energy rebuild phase
  EXPECT_LT(std::abs(out.airspeed_error_m_s), 0.5 * SPD_ERR_AFTER_CLIMB)
      << "speed deficit not rebuilding after altitude capture";

  tickN(controller, aircraft, 9000); // +360 s: full recapture (the
  // deficit halves roughly every two minutes at this depth; full
  // energy recovery from a ~1.3 km excursion takes ~10 min of sim)
  EXPECT_LT(std::abs(out.altitude_error_m), 30.0);
  EXPECT_LT(std::abs(out.airspeed_error_m_s), 3.0)
      << "cruise not fully recaptured 10 min after restore";
}

TEST(AircraftCommandSurface, OrchStateMirrorsIntoFrameAndCountsRecoveries) {
  CelestialBody earth;
  earth.tunables().set(analyticEarth());
  ASSERT_EQ(earth.init(), 0u);
  Aircraft aircraft;
  aircraft.setBody(&earth);
  setTurbulence(aircraft, 0);

  const auto& tlm = aircraft.telemetry();
  EXPECT_EQ(tlm.orch_state, 0u);
  EXPECT_EQ(tlm.recovery_count, 0u);

  // Set-piece entry/exit mirrors verbatim; no recovery counted.
  EXPECT_EQ(sendByteCmd(aircraft, 0x0105u, 1u), 0u);
  EXPECT_EQ(tlm.orch_state, 1u);
  EXPECT_EQ(sendByteCmd(aircraft, 0x0105u, 0u), 0u);
  EXPECT_EQ(tlm.orch_state, 0u);
  EXPECT_EQ(tlm.recovery_count, 0u);

  // Recovery entry counts exactly once per entry, including reason
  // changes within one recovery episode.
  EXPECT_EQ(sendByteCmd(aircraft, 0x0105u, 0x11u), 0u); // speed boundary
  EXPECT_EQ(tlm.recovery_count, 1u);
  EXPECT_EQ(sendByteCmd(aircraft, 0x0105u, 0x12u), 0u); // still recovering
  EXPECT_EQ(tlm.recovery_count, 1u);
  EXPECT_EQ(sendByteCmd(aircraft, 0x0105u, 0u), 0u);    // recovered
  EXPECT_EQ(sendByteCmd(aircraft, 0x0105u, 0x13u), 0u); // next episode
  EXPECT_EQ(tlm.recovery_count, 2u);
  EXPECT_EQ(tlm.orch_state, 0x13u);
}
