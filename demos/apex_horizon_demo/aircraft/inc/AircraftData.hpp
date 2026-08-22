#ifndef APEX_HORIZON_DEMO_AIRCRAFT_DATA_HPP
#define APEX_HORIZON_DEMO_AIRCRAFT_DATA_HPP
/**
 * @file AircraftData.hpp
 * @brief Tunable + state + telemetry structs for the Aircraft.
 *
 * Tunables cover:
 *   - Scalar flight-mechanics model: speed V, heading ψ, flight-path
 *     angle γ, geodetic position. 6-state PointMass3D-driven update.
 *   - Aerodynamics from the drag polar: CL = CL0 + CL_α*α,
 *     CD = CD0 + CL^2/(πeAR). Trim α each tick for L = m*g*cos(γ).
 *   - Propulsion: density-scaled thrust T(throttle, ρ) -- empirical.
 *   - Atmosphere ρ queried from CelestialBody::atmosphere().
 *   - Open-loop "fly the pattern" command schedule (constant throttle +
 *     constant turn rate). The 6DOF path replaces it under closed-loop control.
 *
 * Wire format: AircraftTelemetry is byte-identical with the upcoming
 * horizon-side AircraftFrame contract. 256-byte fixed layout matching
 * the GroundVehicleTelemetry pattern; optional subsystems populate
 * currently-zero fields without bumping the wire version. ANY layout change requires a APP_VERSION
 * bump on both sides.
 *
 * All structs trivially-copyable for TPRM compatibility.
 */

#include <cstdint>

namespace appsim {
namespace aircraft {

/* ----------------------------- AircraftTunables ----------------------------- */

/**
 * @brief Tunable parameters for the Aircraft component.
 *
 * Default values describe a four-engine wide-body jet transport,
 * consistent with the sim_aerodynamics transport cruise preset. The
 * polar coefficients are subsonic-cruise representative (CD0 ~0.020,
 * Oswald e ~0.80, AR ~7).
 *
 * Trivially-copyable; 192 bytes.
 */
struct AircraftTunables {
  /// Wing reference area [m^2] (transport cruise preset value).
  double wing_area_m2{510.97};

  /// Aspect ratio (b^2 / S) matching the preset span and area.
  double aspect_ratio{6.96};

  /// Oswald efficiency factor (~0.7-0.85 typical).
  double oswald_e{0.80};

  /// Maximum sea-level thrust [N], all engines (four-engine transport).
  double thrust_max_sl_N{1.0e6};

  /// Density exponent for thrust scaling. Turbofan ~0.7; turbojet ~1.0.
  double thrust_density_exp{0.7};

  /// Default throttle for the open-loop fallback when no closed-loop
  /// controller is wired (also used as SpeedHold trim throttle).
  double throttle_default{0.6};

  /// Initial geodetic position [deg].
  double init_lat_deg{39.5};
  double init_lon_deg{-105.5};

  /// Initial altitude AMSL [m]. Default = 12,192 m (40,000 ft), the
  /// preset cruise reference condition.
  double init_alt_m{12192.0};

  /// Initial heading [deg from north, clockwise].
  double init_heading_deg{45.0};

  /// Initial true airspeed [m/s]. Default = the preset cruise speed.
  double init_speed_m_s{235.9};

  /* ----------------------------- 6DOF parameters ----------------------------- */

  /// Body-frame inertia tensor about CG [kg*m^2]. Defaults match the
  /// transport cruise preset mass properties. xz-symmetric (Iyx = Iyz = 0).
  double I_xx_kgm2{24675886.0};
  double I_yy_kgm2{44877562.0};
  double I_zz_kgm2{67384138.0};
  double I_xz_kgm2{1315143.0};

  /// Initial body attitude [deg]. Default = 0 (stability-axis convention:
  /// body x along V at trim). Heading is shared with init_heading_deg.
  double init_pitch_deg{0.0};
  double init_roll_deg{0.0};

  /* ----------------------------- Propulsion + mass properties ----------------------------- */

  /// Thrust-specific fuel consumption [kg/(N·s)].
  /// Default = 1.7e-5 kg/(N*s) (~0.6 lbm/(lbf*hr)), transport-cruise
  /// representative.
  double TSFC_kg_per_N_s{1.7e-5};

  /// Initial fuel mass [kg] (wide-body transport class).
  /// FuelBurnMassProperties uses this as both initial state AND tank capacity.
  double fuel_capacity_kg{142000.0};

  /// Operating empty weight [kg] (wide-body transport class).
  /// Total takeoff mass = m_empty_kg + fuel_capacity_kg + payload (ignored).
  /// Note: existing `mass_kg` tunable is now superseded — Aircraft uses
  /// (m_empty + fuel(t)) at runtime. mass_kg kept for backward compat.
  double m_empty_kg{162400.0};

  /// Tag for log lines.
  char body_label[16]{};
};

static_assert(sizeof(AircraftTunables) <= 256, "AircraftTunables fits within a single TPRM page");

/* ----------------------------- AircraftState ----------------------------- */

/**
 * @brief Internal bookkeeping for the Aircraft.
 *
 * Distinct from telemetry: telemetry is the public-face wire frame;
 * this is implementation detail (tick counters, init flag, integrator
 * scratch). 32 bytes.
 */
struct AircraftState {
  /// Number of `aircraftStep` invocations.
  std::uint64_t tick_count{0};

  /// Total simulated time since init [s] (wall-clock equivalent of
  /// tick_count * dt).
  double elapsed_s{0.0};

  /// True after the first tick has applied the initial pose.
  std::uint8_t initialized{0};

  std::uint8_t reserved[15]{};
};

/* ----------------------------- AircraftTelemetry ----------------------------- */

/**
 * @brief Public-face telemetry (OUTPUT) for the Aircraft. 256 bytes.
 *
 * Wire-format-aligned with the upcoming horizon-side AircraftContract.hpp.
 * Layout chosen so optional subsystems can populate currently-zero
 * fields (fuel_kg, AGL altitude, engine state) without changing byte
 * offsets or the wire version.
 *
 * If you change this layout you MUST bump the corresponding APP_VERSION
 * in horizon's AircraftContract.hpp and the ShmRingBridge tunable.
 */
struct AircraftTelemetry {
  /// Producer-side monotonic timestamp at the moment aircraftStep ran [ns].
  std::uint64_t timestamp_ns{0};
  /// Tick count at which this snapshot was produced.
  std::uint64_t tick{0};

  // --- Pose (56 bytes) ---
  double pos_lat_deg{0.0};
  double pos_lon_deg{0.0};
  double pos_alt_m{0.0};
  double heading_deg{0.0};
  double pitch_deg{0.0}; ///< populated by the 6DOF path
  double roll_deg{0.0};  ///< populated by the 6DOF path
  double gamma_deg{0.0}; ///< flight-path angle

  // --- Velocity + airdata (32 bytes) ---
  double airspeed_m_s{0.0}; ///< true airspeed
  double groundspeed_m_s{0.0};
  double vertical_speed_m_s{0.0};
  double alpha_rad{0.0}; ///< angle of attack

  // --- Forces + coefficients (40 bytes) ---
  double mach{0.0};
  double dynamic_pressure_Pa{0.0};
  double lift_N{0.0};
  double drag_N{0.0};
  double thrust_N{0.0};

  // --- Mass + density (24 bytes) ---
  double mass_kg{0.0};
  double fuel_kg{0.0}; ///< populated with fuel burn active
  double rho_kg_m3{0.0};

  // --- Commanded inputs + sensor-derived (32 bytes) ---
  double throttle{0.0};
  double bank_cmd_deg{0.0};
  double pitch_cmd_deg{0.0};
  double altitude_AGL_m{0.0}; ///< populated when a radar altimeter is wired

  // --- Flags + small fields (8 bytes) ---
  std::uint8_t mode{0};         ///< 0 = open-loop pattern; 1 = closed-loop
  std::uint8_t engine_state{0}; ///< N1/N2 spool state (populated when wired)
  std::uint16_t reserved0{0};
  std::uint32_t reserved1{0};

  // --- Body angular rates (24 bytes) ---
  double p_rad_s{0.0}; ///< body roll rate
  double q_rad_s{0.0}; ///< body pitch rate
  double r_rad_s{0.0}; ///< body yaw rate

  // --- Control-surface deflections (24 bytes) ---
  double elevator_rad{0.0};
  double aileron_rad{0.0};
  double rudder_rad{0.0};

  /// Wire layout NOTE: the rates/surfaces block replaced an all-zero
  /// reserved tail byte-compatibly (pre-existing readers parsed zeros).
  /// Further additions need a new reserved tail or a wire version bump.
};

static_assert(sizeof(AircraftTelemetry) == 256,
              "AircraftTelemetry must be 256 bytes (matches wire-format AircraftFrame)");

} // namespace aircraft
} // namespace appsim

#endif // APEX_HORIZON_DEMO_AIRCRAFT_DATA_HPP
