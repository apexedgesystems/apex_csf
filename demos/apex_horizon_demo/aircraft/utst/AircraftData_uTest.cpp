/**
 * @file AircraftData_uTest.cpp
 * @brief Wire-format + default-value tests for AircraftTunables /
 *        AircraftState / AircraftTelemetry.
 *
 * Wire-format size is enforced via static_assert in the header; this
 * file adds runtime checks on default values so a silent drift in a
 * tunable struct definition trips a test failure.
 */

#include "demos/apex_horizon_demo/aircraft/inc/AircraftData.hpp"

#include <gtest/gtest.h>

using appsim::aircraft::AircraftState;
using appsim::aircraft::AircraftTelemetry;
using appsim::aircraft::AircraftTunables;

/* ----------------------------- Sizes (wire format) ----------------------------- */

/** @test Telemetry size is exactly 256 bytes (matches AircraftFrame). */
TEST(AircraftDataTest, TelemetryIsExactly256Bytes) { EXPECT_EQ(sizeof(AircraftTelemetry), 256u); }

/** @test State size is small + stable (changes here ripple to TPRM bin). */
TEST(AircraftDataTest, StateSizeIsExpected) {
  // 8 (tick) + 8 (elapsed) + 1 (init) + 15 (reserved) = 32
  EXPECT_EQ(sizeof(AircraftState), 32u);
}

/** @test Tunables fit within a single TPRM page (256B). */
TEST(AircraftDataTest, TunablesFitWithinTprmPage) { EXPECT_LE(sizeof(AircraftTunables), 256u); }

/* ----------------------------- Default values ----------------------------- */

/** @test Tunable defaults match the transport cruise preset reference condition. */
TEST(AircraftDataTest, TunableDefaultsMatchTransportCruisePreset) {
  AircraftTunables p;
  EXPECT_DOUBLE_EQ(p.mass_kg, 288800.0);
  EXPECT_DOUBLE_EQ(p.wing_area_m2, 510.97);
  EXPECT_DOUBLE_EQ(p.aspect_ratio, 6.96);
  EXPECT_DOUBLE_EQ(p.oswald_e, 0.80);
  EXPECT_DOUBLE_EQ(p.CD0, 0.020);
  EXPECT_DOUBLE_EQ(p.CL0, 0.10);
  EXPECT_DOUBLE_EQ(p.CL_alpha, 5.50);
  EXPECT_DOUBLE_EQ(p.thrust_max_sl_N, 1.0e6);
  EXPECT_DOUBLE_EQ(p.thrust_density_exp, 0.7);
  EXPECT_DOUBLE_EQ(p.throttle_default, 0.6);

  // Inertia tensor (preset mass properties):
  EXPECT_DOUBLE_EQ(p.I_xx_kgm2, 24675886.0);
  EXPECT_DOUBLE_EQ(p.I_yy_kgm2, 44877562.0);
  EXPECT_DOUBLE_EQ(p.I_zz_kgm2, 67384138.0);
  EXPECT_DOUBLE_EQ(p.I_xz_kgm2, 1315143.0);
}

/** @test Initial pose defaults match the cruise reference condition + demo patch. */
TEST(AircraftDataTest, InitPoseDefaultsAreReasonable) {
  AircraftTunables p;
  EXPECT_DOUBLE_EQ(p.init_alt_m, 12192.0);   // 40,000 ft cruise reference
  EXPECT_DOUBLE_EQ(p.init_speed_m_s, 235.9); // preset cruise speed
  EXPECT_DOUBLE_EQ(p.init_pitch_deg, 0.0);   // stab-axis convention: trim
  EXPECT_DOUBLE_EQ(p.init_roll_deg, 0.0);
  EXPECT_DOUBLE_EQ(p.init_lat_deg, 39.5); // Colorado patch
  EXPECT_DOUBLE_EQ(p.init_lon_deg, -105.5);
}

/** @test Telemetry default-init has tick=0, zero forces, zero rates/surfaces. */
TEST(AircraftDataTest, TelemetryDefaultInitialization) {
  AircraftTelemetry tlm;
  EXPECT_EQ(tlm.tick, 0u);
  EXPECT_DOUBLE_EQ(tlm.lift_N, 0.0);
  EXPECT_DOUBLE_EQ(tlm.drag_N, 0.0);
  EXPECT_DOUBLE_EQ(tlm.thrust_N, 0.0);
  EXPECT_DOUBLE_EQ(tlm.airspeed_m_s, 0.0);

  // Body rates + control surfaces.
  EXPECT_DOUBLE_EQ(tlm.p_rad_s, 0.0);
  EXPECT_DOUBLE_EQ(tlm.q_rad_s, 0.0);
  EXPECT_DOUBLE_EQ(tlm.r_rad_s, 0.0);
  EXPECT_DOUBLE_EQ(tlm.elevator_rad, 0.0);
  EXPECT_DOUBLE_EQ(tlm.aileron_rad, 0.0);
  EXPECT_DOUBLE_EQ(tlm.rudder_rad, 0.0);
}

/** @test State default-init: not yet initialized + zero counters. */
TEST(AircraftDataTest, StateDefaultInitialization) {
  AircraftState s;
  EXPECT_EQ(s.tick_count, 0u);
  EXPECT_DOUBLE_EQ(s.elapsed_s, 0.0);
  EXPECT_EQ(s.initialized, 0u);
}
