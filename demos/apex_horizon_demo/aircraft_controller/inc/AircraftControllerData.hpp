#ifndef APEX_HORIZON_DEMO_AIRCRAFT_CONTROLLER_DATA_HPP
#define APEX_HORIZON_DEMO_AIRCRAFT_CONTROLLER_DATA_HPP
/**
 * @file AircraftControllerData.hpp
 * @brief Tunable + state + output structs for the AircraftController.
 *
 * AircraftController wraps six classical autopilot loops (PitchAttitudeHold,
 * AltitudeHold, SpeedHold, RollController, HeadingHold, YawDamper) into a
 * separate apex SwModelBase. It runs at 25 Hz against the Aircraft model's
 * 50 Hz, so each control update sees two state ticks of integration in
 * between (the classical loop-shaping treatment.2 cascade architecture: inner loop ≥ 2× outer
 * rate).
 *
 * Cascade tree:
 *
 *   AltitudeHold (h_ref → θ_ref) → PitchAttitudeHold (θ_ref → δe)
 *                                                    ↑
 *   HeadingHold (ψ_ref → φ_ref) → RollController (φ_ref → δa)
 *                                                  ↑
 *                                              YawDamper (r → δr)
 *
 *   SpeedHold (V_ref → throttle)            (decoupled: cruise control)
 *
 * All structs trivially-copyable for TPRM compatibility.
 */

#include <cstdint>

namespace appsim {
namespace aircraft_controller {

/* --------------------------- AircraftControllerTunables --------------------------- */

/**
 * Setpoints + per-loop PID gains + saturation limits.
 *
 * Defaults match the wide-body transport values in
 * `sim::gnc::aircraft::LongitudinalControllers.hpp` and `LateralControllers.hpp`,
 * tuned around the classical loop-shaping treatment.
 */
struct AircraftControllerTunables {
  /* ---- Setpoints (commanded) ---- */
  double target_altitude_m{8000.0};
  double target_airspeed_m_s{240.0};
  double target_heading_deg{45.0};
  double trim_throttle{0.50};

  /* ---- Pitch attitude hold (inner: θ → δe) ---- */
  double pitch_Kp{1.50};
  double pitch_Ki{0.10};
  double pitch_Kd{0.40};
  double elevator_limit_rad{0.35};

  /* ---- Altitude hold (outer: h → θ_ref) ---- */
  double alt_Kp{0.0008};
  double alt_Ki{0.00004};
  double alt_Kd{0.0050};
  double pitch_ref_limit_rad{0.15};

  /* ---- Speed hold (V → throttle) ---- */
  double speed_Kp{0.020};
  double speed_Ki{0.0010};
  double speed_Kd{0.00};

  /* ---- Roll controller (inner: φ → δa) ---- */
  double roll_Kp{0.80};
  double roll_Ki{0.05};
  double roll_Kd{0.30};
  double aileron_limit_rad{0.35};

  /* ---- Heading hold (outer: ψ → φ_ref) ---- */
  double hdg_Kp{1.20};
  double hdg_Ki{0.05};
  double hdg_Kd{0.20};
  double bank_limit_rad{0.45};

  /* ---- Yaw damper (r → δr, the classical loop-shaping treatment.10) ---- */
  double yaw_Kr{0.80};
  double rudder_limit_rad{0.30};
  /// High-pass washout time constant [s]. Passes Dutch-roll band, rejects
  /// the steady turn rate during coordinated bank. 0 disables washout
  /// (pure P feedback). the classical loop-shaping treatment.10 typical: 3 s for transport
  /// aircraft.
  double yaw_tau_w_s{3.0};

  /* ---- GustAlleviation feedforward (vertical-gust → δe) ---- */
  /// CL_α / CL_δe ratio (preset cruise: 4.67/0.32 ≈ 14.6).
  double gust_K_alpha_over_delta{14.6};
  /// Authority fraction; 0 = disabled, 1 = full feedforward.
  double gust_authority_pct{0.0};

  /* ---- Mode + label ---- */
  std::uint8_t enable_mode{0}; ///< 0 = pass-through (zero outputs); 1 = closed-loop
  std::uint8_t reserved[7]{};
  char label[16]{};
};

static_assert(sizeof(AircraftControllerTunables) <= 256,
              "AircraftControllerTunables fits within a single TPRM page");

/* --------------------------- AircraftControllerState --------------------------- */

/** Internal bookkeeping for the AircraftController. */
struct AircraftControllerState {
  std::uint64_t tick_count{0};
  double elapsed_s{0.0};
  std::uint8_t initialized{0};
  std::uint8_t reserved[15]{};
};

/* --------------------------- AircraftControllerOutput --------------------------- */

/**
 * Publically-visible OUTPUT block. Aircraft consumes the four control
 * surface fields each tick; the rest is diagnostic telemetry.
 *
 * Not wire-format-aligned (this stays inside apex; only AircraftTelemetry
 * crosses the bridge to UE5).
 */
struct AircraftControllerOutput {
  std::uint64_t timestamp_ns{0};
  std::uint64_t tick{0};

  /* ---- Control surface commands (consumed by Aircraft) ---- */
  double elevator_rad{0.0};
  double aileron_rad{0.0};
  double rudder_rad{0.0};
  double throttle{0.0};

  /* ---- Cascaded references for diagnostics ---- */
  double pitch_ref_rad{0.0};
  double bank_ref_rad{0.0};

  /* ---- Errors for diagnostics ---- */
  double altitude_error_m{0.0};
  double airspeed_error_m_s{0.0};
  double heading_error_rad{0.0};

  /* ---- Mode echo + reserved tail ---- */
  std::uint8_t mode{0};
  std::uint8_t reserved[7]{};
  std::uint8_t reserved2[64]{};
};

} // namespace aircraft_controller
} // namespace appsim

#endif // APEX_HORIZON_DEMO_AIRCRAFT_CONTROLLER_DATA_HPP
