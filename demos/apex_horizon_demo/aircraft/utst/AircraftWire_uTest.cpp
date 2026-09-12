/**
 * @file AircraftWire_uTest.cpp
 * @brief Wire-layout pin for the ACFT frame + mode-excitation coverage.
 *
 * The 256-byte AircraftTelemetry IS the wire contract: the consumer
 * side byte-diffs its contract header against this layout at pairing.
 * Every field offset is pinned here so any drift fails this suite
 * before it can fail an attach.
 *
 * Excitation tests drive the scripted mode perturbations against an
 * analytic (data-file-free) Earth and assert the surfaces they touch,
 * the windows they occupy, and their self-clearing behavior.
 */

#include "demos/apex_horizon_demo/aircraft/inc/Aircraft.hpp"

#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/factory/inc/Body.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFidelity.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

using appsim::aircraft::Aircraft;
using appsim::aircraft::AircraftTelemetry;
using sim::environment::AtmosphereFidelity;
using sim::environment::Body;
using sim::environment::GravityFidelity;
using sim::environment::TerrainFidelity;
using sim::environment::celestial_body::CelestialBody;
using sim::environment::celestial_body::CelestialBodyTunables;

/* ----------------------------- Wire layout ----------------------------- */

/** @test The ACFT frame layout is frozen (unchanged since v1): 256 bytes,
 *  every offset pinned. ACFT/2 revs the command surface, not the frame. */
TEST(AircraftWire, FrameLayoutIsFrozen) {
  static_assert(sizeof(AircraftTelemetry) == 256, "ACFT frame is exactly 256 bytes");

  EXPECT_EQ(offsetof(AircraftTelemetry, timestamp_ns), 0u);
  EXPECT_EQ(offsetof(AircraftTelemetry, tick), 8u);
  EXPECT_EQ(offsetof(AircraftTelemetry, pos_lat_deg), 16u);
  EXPECT_EQ(offsetof(AircraftTelemetry, pos_lon_deg), 24u);
  EXPECT_EQ(offsetof(AircraftTelemetry, pos_alt_m), 32u);
  EXPECT_EQ(offsetof(AircraftTelemetry, heading_deg), 40u);
  EXPECT_EQ(offsetof(AircraftTelemetry, pitch_deg), 48u);
  EXPECT_EQ(offsetof(AircraftTelemetry, roll_deg), 56u);
  EXPECT_EQ(offsetof(AircraftTelemetry, gamma_deg), 64u);
  EXPECT_EQ(offsetof(AircraftTelemetry, airspeed_m_s), 72u);
  EXPECT_EQ(offsetof(AircraftTelemetry, groundspeed_m_s), 80u);
  EXPECT_EQ(offsetof(AircraftTelemetry, vertical_speed_m_s), 88u);
  EXPECT_EQ(offsetof(AircraftTelemetry, alpha_rad), 96u);
  EXPECT_EQ(offsetof(AircraftTelemetry, mach), 104u);
  EXPECT_EQ(offsetof(AircraftTelemetry, dynamic_pressure_Pa), 112u);
  EXPECT_EQ(offsetof(AircraftTelemetry, lift_N), 120u);
  EXPECT_EQ(offsetof(AircraftTelemetry, drag_N), 128u);
  EXPECT_EQ(offsetof(AircraftTelemetry, thrust_N), 136u);
  EXPECT_EQ(offsetof(AircraftTelemetry, mass_kg), 144u);
  EXPECT_EQ(offsetof(AircraftTelemetry, fuel_kg), 152u);
  EXPECT_EQ(offsetof(AircraftTelemetry, rho_kg_m3), 160u);
  EXPECT_EQ(offsetof(AircraftTelemetry, throttle), 168u);
  EXPECT_EQ(offsetof(AircraftTelemetry, bank_cmd_deg), 176u);
  EXPECT_EQ(offsetof(AircraftTelemetry, pitch_cmd_deg), 184u);
  EXPECT_EQ(offsetof(AircraftTelemetry, altitude_AGL_m), 192u);
  EXPECT_EQ(offsetof(AircraftTelemetry, mode), 200u);
  EXPECT_EQ(offsetof(AircraftTelemetry, engine_state), 201u);
  EXPECT_EQ(offsetof(AircraftTelemetry, orch_state), 202u);
  EXPECT_EQ(offsetof(AircraftTelemetry, recovery_count), 203u);
  EXPECT_EQ(offsetof(AircraftTelemetry, loop_mask), 204u);
  EXPECT_EQ(offsetof(AircraftTelemetry, reserved1), 205u);
  EXPECT_EQ(offsetof(AircraftTelemetry, p_rad_s), 208u);
  EXPECT_EQ(offsetof(AircraftTelemetry, q_rad_s), 216u);
  EXPECT_EQ(offsetof(AircraftTelemetry, r_rad_s), 224u);
  EXPECT_EQ(offsetof(AircraftTelemetry, elevator_rad), 232u);
  EXPECT_EQ(offsetof(AircraftTelemetry, aileron_rad), 240u);
  EXPECT_EQ(offsetof(AircraftTelemetry, rudder_rad), 248u);
}

/* ----------------------------- Excitation ----------------------------- */

namespace {

CelestialBodyTunables analyticEarth() {
  CelestialBodyTunables t{};
  t.body = Body::EARTH;
  t.gravity_fidelity = GravityFidelity::J2;
  t.terrain_fidelity = TerrainFidelity::ELLIPSOID;
  t.atmosphere_fidelity = AtmosphereFidelity::EXPONENTIAL;
  return t;
}

} // namespace

/** @test A rudder doublet plays +/- over its window on the wire and
 *  self-clears; the surfaces return to the commanded (zero) baseline. */
TEST(AircraftWire, CommandSnapshotLayoutIsFrozen) {
  using appsim::aircraft::AircraftCommandSnapshot;
  static_assert(sizeof(AircraftCommandSnapshot) == 16, "snapshot is 16 bytes on the wire");
  static_assert(offsetof(AircraftCommandSnapshot, turbulence_enabled) == 0);
  static_assert(offsetof(AircraftCommandSnapshot, gust_alleviation_enabled) == 1);
  // ACFT/2: the two readback fields land in former reserved bytes --
  // offsets are wire contract, frozen with the consumer.
  static_assert(offsetof(AircraftCommandSnapshot, loop_enable_mask) == 2);
  static_assert(offsetof(AircraftCommandSnapshot, active_excite_mode) == 3);
  static_assert(offsetof(AircraftCommandSnapshot, cmd_count) == 8);
  SUCCEED();
}

TEST(AircraftExcite, RudderDoubletPlaysAndClears) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  Aircraft ac;
  ac.setBody(&earth);
  ASSERT_EQ(ac.init(), 0u);

  ac.startExcitation(Aircraft::ExciteMode::RUDDER_DOUBLET);
  ASSERT_EQ(ac.aircraftStep(), 0u); // t ~ 0: first half of the doublet
  EXPECT_DOUBLE_EQ(ac.telemetry().rudder_rad, 0.05);

  // Advance past the first half (0.5 s at 100 Hz = 50 ticks).
  for (int i = 0; i < 51; ++i) {
    ASSERT_EQ(ac.aircraftStep(), 0u);
  }
  EXPECT_DOUBLE_EQ(ac.telemetry().rudder_rad, -0.05); // second half

  // Advance past the doublet end; excitation must self-clear.
  for (int i = 0; i < 60; ++i) {
    ASSERT_EQ(ac.aircraftStep(), 0u);
  }
  EXPECT_EQ(ac.activeExcitation(), Aircraft::ExciteMode::NONE);
  EXPECT_DOUBLE_EQ(ac.telemetry().rudder_rad, 0.0);
}

/** @test SPEED_OFFSET bumps airspeed once by 10 m/s and immediately
 *  clears (one-shot, no surface involvement). */
TEST(AircraftExcite, SpeedOffsetIsOneShot) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  Aircraft ac;
  ac.setBody(&earth);
  ASSERT_EQ(ac.init(), 0u);

  ASSERT_EQ(ac.aircraftStep(), 0u); // seed 6DOF state at init speed
  const double V0 = ac.telemetry().airspeed_m_s;

  ac.startExcitation(Aircraft::ExciteMode::SPEED_OFFSET);
  ASSERT_EQ(ac.aircraftStep(), 0u);
  EXPECT_EQ(ac.activeExcitation(), Aircraft::ExciteMode::NONE);
  EXPECT_NEAR(ac.telemetry().airspeed_m_s, V0 + 10.0, 0.5);
  EXPECT_DOUBLE_EQ(ac.telemetry().rudder_rad, 0.0);
  EXPECT_DOUBLE_EQ(ac.telemetry().elevator_rad, 0.0);
}

/** @test Timestamps sit on the sim tick grid: consecutive published
 *  frames differ by exactly one tick period, so scheduler jitter never
 *  reaches the wire and consumer interpolators see a clean timeline. */
TEST(AircraftWire, TimestampsOnTickGrid) {
  CelestialBody earth;
  earth.tunables().get() = analyticEarth();
  ASSERT_EQ(earth.init(), 0u);

  Aircraft ac;
  ac.setBody(&earth);
  ASSERT_EQ(ac.init(), 0u);

  ASSERT_EQ(ac.aircraftStep(), 0u);
  const std::uint64_t T1 = ac.telemetry().timestamp_ns;
  for (int i = 0; i < 5; ++i) {
    ASSERT_EQ(ac.aircraftStep(), 0u);
  }
  const std::uint64_t T2 = ac.telemetry().timestamp_ns;
  EXPECT_EQ(T2 - T1, 5u * 10'000'000u); // 5 ticks at 100 Hz, exact
}
