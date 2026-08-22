#ifndef APEX_HORIZON_DEMO_AIRCRAFT_HPP
#define APEX_HORIZON_DEMO_AIRCRAFT_HPP
/**
 * @file Aircraft.hpp
 * @brief Aircraft SW model. Composes the sim/ primitives.
 *
 * Each `aircraftStep` tick (100 Hz, the executive fundamental):
 *
 *   1. Read controls from the AircraftController OUTPUT block (or
 *      open-loop defaults), superimpose any armed mode excitation.
 *   2. Query atmosphere ρ at current altitude (CelestialBody dispatch);
 *      step Dryden turbulence to get the apparent wind; Pitot IAS.
 *   3. Step the two-spool turbofans (thrust lags throttle) and the
 *      fuel-burn mass source (mass + inertia for this tick).
 *   4. RK4-integrate RigidBody6DOF under stability-derivative aero,
 *      gravity rotated into the body frame, thrust along body x, and
 *      the engine-rotor gyroscopic moment.
 *   5. Reverse-project the NED state → telemetry (lat/lon/alt, Euler
 *      attitude, airdata, rates, surfaces) — the 256-byte wire frame.
 *
 * The `AircraftController` SW model supplies closed-loop surface and
 * throttle commands; with no controller wired the aircraft falls back
 * to open-loop defaults.
 *
 * @note RT-safe within tick (no allocation); logging is NOT RT-safe.
 */

#include "demos/apex_horizon_demo/aircraft/inc/AircraftData.hpp"
#include "demos/apex_horizon_demo/aircraft_controller/inc/AircraftControllerData.hpp"

#include "src/sim/aerodynamics/inc/PolarAero.hpp"
#include "src/sim/aerodynamics/inc/StabilityDerivativeAero.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "demos/apex_horizon_demo/aircraft/inc/AircraftCommand.hpp"
#include "src/sim/dynamics/disturbance/inc/DrydenTurbulence.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/sim/dynamics/mass_properties/inc/FuelBurnMassProperties.hpp"
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp"
#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"
#include "src/sim/propulsion/inc/DensityScaledThrust.hpp"
#include "src/sim/propulsion/inc/Turbofan2Spool.hpp"
#include "src/sim/sensors/inc/Pitot.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"
#include "src/utilities/helpers/inc/Cpu.hpp"
#include "src/utilities/helpers/inc/Files.hpp"
#include "src/utilities/math/integration/inc/Quaternion.hpp"
#include "src/utilities/math/vecmat/inc/Angles.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <optional>
#include <string>
#include <system_error>

namespace appsim {
namespace aircraft {

using ApexStatus = system_core::system_component::Status;

/* ----------------------------- Constants ----------------------------- */

namespace {
using apex::math::vecmat::DEG_TO_RAD;
using apex::math::vecmat::RAD_TO_DEG;
constexpr double GRAVITY_M_S2 = 9.80665;
/// Reference speed of sound at 15°C sea level [m/s]. Used for an
/// approximate Mach number until temperature-aware Mach lands.
constexpr double A_SL_M_S = 340.3;
/// Number of engines for the four-engine transport airframe.
/// Hardcoded: exposing it as a TPRM tunable would mean re-validating
/// Turbofan2Spool parameters per-vehicle; other vehicle classes get
/// their own derived components.
constexpr int NUM_ENGINES = 4;
/// Tick step. 100 Hz to match the executive fundamental (set in
/// executive.toml — the two must change together). The scheduler
/// dispatches aircraftStep every fundamental tick so dt = 1/100 s:
/// far above the short-period mode (~1 rad/s), and the telemetry
/// cadence the consumer's timeline interpolator renders from.
constexpr double DT_S = 1.0 / 100.0;
} // namespace

/* ----------------------------- Aircraft ----------------------------- */

class Aircraft final : public system_core::system_component::SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component class ID. Picked sequentially after GroundVehicle (222).
  /// 223 is reserved for a future GroundVehicleController; aircraft is 224.
  static constexpr std::uint16_t COMPONENT_ID = 224;
  static constexpr const char* COMPONENT_NAME = "Aircraft";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "AIRCRAFT"; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    AIRCRAFT_STEP = 1, ///< Periodic flight-mechanics step + aero/thrust eval.
    TELEMETRY = 2,     ///< Periodic log line.
  };

  /* ----------------------------- Mode excitation ----------------------------- */

  /// Scripted control perturbations from trim, one per classical
  /// dynamic mode. Superimposed on the controller's surface commands
  /// for a fixed window, then auto-cleared — the demo's
  /// dampers-off / excite / dampers-on sequence uses these, and the
  /// unit suite drives them directly. Wire exposure (an EXCITE_MODE
  /// opcode) is an ACFT/2 addition; the machinery is wire-independent.
  enum class ExciteMode : std::uint8_t {
    NONE = 0,
    RUDDER_DOUBLET = 1, ///< +/-0.05 rad, 0.5 s each: Dutch roll.
    ELEVATOR_PULSE = 2, ///< -0.05 rad for 0.5 s: short period + phugoid.
    AILERON_PULSE = 3,  ///< +0.02 rad for 0.5 s: roll/spiral response.
    SPEED_OFFSET = 4,   ///< One-shot +10 m/s along body x: phugoid-selective.
  };

  /// Arm an excitation; it plays out over subsequent aircraftStep
  /// ticks and clears itself. Arming while one is active restarts.
  void startExcitation(ExciteMode mode) noexcept {
    excite_mode_ = mode;
    excite_t_s_ = 0.0;
    excite_speed_applied_ = false;
  }
  [[nodiscard]] ExciteMode activeExcitation() const noexcept { return excite_mode_; }

  /* ----------------------------- Construction ----------------------------- */

  Aircraft() noexcept = default;
  ~Aircraft() override = default;

  Aircraft(const Aircraft&) = delete;
  Aircraft& operator=(const Aircraft&) = delete;

  /* ----------------------------- Wiring ----------------------------- */

  /// Set the CelestialBody whose atmosphere this aircraft flies through.
  /// Pointer must outlive the component. Call before init.
  void setBody(const sim::environment::celestial_body::CelestialBody* body) noexcept {
    body_ = body;
  }
  [[nodiscard]] const sim::environment::celestial_body::CelestialBody* body() const noexcept {
    return body_;
  }

  /// Set the AircraftController OUTPUT block to consume. Pointer must
  /// outlive the component. If nullptr, the aircraft falls back to
  /// open-loop defaults (zero control surfaces, trim throttle).
  void
  setControllerOutput(const appsim::aircraft_controller::AircraftControllerOutput* out) noexcept {
    controller_output_ = out;
  }
  [[nodiscard]] const appsim::aircraft_controller::AircraftControllerOutput*
  controllerOutput() const noexcept {
    return controller_output_;
  }

  /* ----------------------------- Tunables / state accessors ----------------------------- */

  [[nodiscard]] system_core::data::TunableParam<AircraftTunables>& tunables() noexcept {
    return tunables_;
  }
  [[nodiscard]] const AircraftState& aircraftState() const noexcept { return state_.get(); }
  [[nodiscard]] const AircraftTelemetry& telemetry() const noexcept { return telemetry_.get(); }

  /* ----------------------------- Tasks ----------------------------- */

  /// One 6DOF flight-mechanics step.
  ///
  /// Pipeline:
  ///   1. First tick: seed RigidBody6DOFState from tunables (NED inertial
  ///      origin at init pose; body velocity (V, 0, 0); attitude from
  ///      heading/pitch/roll Euler ZYX; zero body rates).
  ///   2. Read controls from `controller_output_` (or zeros if null).
  ///   3. Atmosphere lookup at current altitude.
  ///   4. RK4 step of (gravity-rotated-to-body + stab-deriv aero +
  ///      density-scaled thrust along body x).
  ///   5. Reverse-project NED state → telemetry (lat/lon/alt + Euler).
  std::uint8_t aircraftStep() noexcept {
    auto& s = state_.get();
    auto& tlm = telemetry_.get();
    const auto& p = tunables_.get();

    if (body_ == nullptr || !body_->isReady()) {
      ++s.tick_count;
      return 0u;
    }

    // First tick: seed 6DOF state from tunables.
    if (s.initialized == 0u) {
      initRigidBodyState_(p);
      s.initialized = 1u;
    }

    // 1. Read controls from controller (or zeros for open-loop fallback).
    sim::aerodynamics::ControlInputs delta{};
    double throttle = p.throttle_default;
    std::uint8_t controller_mode = 0u;
    double pitch_ref_rad = 0.0;
    double bank_ref_rad = 0.0;
    if (controller_output_ != nullptr) {
      delta.elevator_rad = controller_output_->elevator_rad;
      delta.aileron_rad = controller_output_->aileron_rad;
      delta.rudder_rad = controller_output_->rudder_rad;
      throttle = controller_output_->throttle;
      controller_mode = controller_output_->mode;
      pitch_ref_rad = controller_output_->pitch_ref_rad;
      bank_ref_rad = controller_output_->bank_ref_rad;
    }

    // 1b. Superimpose any armed mode excitation on the commanded
    //     surfaces (scripted perturbations from trim; self-clearing).
    applyExcitation_(delta);

    // 2. Atmosphere lookup at current altitude (NED: alt = -position.z).
    const double altitude_amsl_m = -rb_state_.position_inertial.z;
    const double lat_rad = tlm.pos_lat_deg * DEG_TO_RAD;
    const double lon_rad = tlm.pos_lon_deg * DEG_TO_RAD;
    double rho = 0.0;
    if (const auto* atmo = body_->atmosphere(); atmo != nullptr) {
      (void)atmo->density(altitude_amsl_m, lat_rad, lon_rad, rho);
    }

    // 2b. Dryden turbulence step. Uses last-tick airspeed (V from
    //     rb_state_.velocity_body magnitude is fine for the quasi-stationary
    //     time-scale Dryden PSD assumes). Gust velocities subtract from
    //     body velocity to give the aircraft's apparent wind for aero.
    const double V_for_dryden = std::sqrt(rb_state_.velocity_body.x * rb_state_.velocity_body.x +
                                          rb_state_.velocity_body.y * rb_state_.velocity_body.y +
                                          rb_state_.velocity_body.z * rb_state_.velocity_body.z);
    const auto gust = sim::dynamics::disturbance::stepDryden(
        dryden_state_, dryden_params_, dryden_rng_, V_for_dryden > 1.0 ? V_for_dryden : 1.0, DT_S);
    // Gate via runtime command. Filter state continues to advance
    // when disabled so re-enabling resumes without a phase transient;
    // we just zero what gets applied to the apparent wind.
    const double gust_scale = turbulence_enabled_ ? 1.0 : 0.0;
    latest_u_g_m_s_ = gust.u_g_m_s * gust_scale;
    latest_v_g_m_s_ = gust.v_g_m_s * gust_scale;
    latest_w_g_m_s_ = gust.w_g_m_s * gust_scale;
    // Apparent wind for aero (V_relative = V_body - V_gust_body).
    const sim::dynamics::rigid_body::Vec3 v_apparent_body{
        rb_state_.velocity_body.x - latest_u_g_m_s_, rb_state_.velocity_body.y - latest_v_g_m_s_,
        rb_state_.velocity_body.z - latest_w_g_m_s_};

    // 2c. Pitot indicated airspeed from V_apparent + ρ.
    const double V_apparent_mag =
        std::sqrt(v_apparent_body.x * v_apparent_body.x + v_apparent_body.y * v_apparent_body.y +
                  v_apparent_body.z * v_apparent_body.z);
    latest_IAS_m_s_ = pitot_.indicatedAirspeed(V_apparent_mag, rho);

    // 3. Step Turbofan2Spool (per-engine, then sum across NUM_ENGINES).
    //    N1/N2 spools have first-order lag → thrust this tick is from
    //    last tick's spool state, not the throttle command directly.
    //    Spool lag means thrust responds over seconds, not instantly.
    turbofan_params_.T_max_sl_N = p.thrust_max_sl_N / NUM_ENGINES;
    turbofan_params_.rho_ref_kg_m3 = 1.225;
    turbofan_params_.n_density = p.thrust_density_exp;
    const auto tf =
        sim::propulsion::stepTurbofan2Spool(turbofan_state_, turbofan_params_, throttle, rho, DT_S);
    const double T_total_N = tf.thrust_N * NUM_ENGINES;
    const double H_rotor_total = tf.H_rotor_kgm2_s * NUM_ENGINES; // engines aligned body x

    // 4. Step fuel burn → updated mass + inertia for this tick.
    //    Fuel flow is TSFC * thrust (the defining relation of TSFC).
    //    The tank sits at the body reference point, so total inertia is
    //    dry inertia + the tank's fuel-fraction-scaled contribution with
    //    no parallel-axis term. The dry/fuel split is inferred from the
    //    full tensor by mass-ratio scaling (rough — assumes uniform
    //    fuel distribution; a tank-distribution-aware model would
    //    refine it).
    fuel_tank_.params.TSFC_kg_per_N_s = p.TSFC_kg_per_N_s;
    fuel_tank_.params.fuel_capacity_kg = p.fuel_capacity_kg;
    (void)fuel_tank_.step(T_total_N, DT_S);
    const double m_total_kg = p.m_empty_kg + fuel_tank_.fuel_kg;
    const auto TANK = fuel_tank_.current();
    const sim::dynamics::rigid_body::InertiaTensor I_now{
        inertia_dry_.Ixx + TANK.inertia_about_own_cg.Ixx,
        inertia_dry_.Iyy + TANK.inertia_about_own_cg.Iyy,
        inertia_dry_.Izz + TANK.inertia_about_own_cg.Izz,
        inertia_dry_.Ixz + TANK.inertia_about_own_cg.Ixz};

    // 5. Force / moment lambdas. Closed over (rho, throttle, delta, mass, T_total).
    //    Used by the RK4 4-stage integrator; they sample state at the
    //    intermediate stage points so the lambdas read `st`, not rb_state_.
    auto force_fn = [&](double, const sim::dynamics::rigid_body::RigidBody6DOFState& st) {
      // Apparent wind for aero = body velocity − gust (frozen across RK stages).
      const sim::dynamics::rigid_body::Vec3 v_app{st.velocity_body.x - latest_u_g_m_s_,
                                                  st.velocity_body.y - latest_v_g_m_s_,
                                                  st.velocity_body.z - latest_w_g_m_s_};
      const auto a = sim::aerodynamics::evaluateStabilityDerivative(
          aero_params_, v_app, st.angular_velocity_body, delta, rho);
      // Gravity in NED inertial = (0, 0, +mg). Rotate to body via attitude.conjugate.
      const auto q_conj = st.attitude.conjugate();
      const auto g_body = q_conj.rotate(0.0, 0.0, +m_total_kg * GRAVITY_M_S2);
      return sim::dynamics::rigid_body::Vec3{a.force_body.x + g_body[0] + T_total_N,
                                             a.force_body.y + g_body[1],
                                             a.force_body.z + g_body[2]};
    };
    auto moment_fn = [&](double, const sim::dynamics::rigid_body::RigidBody6DOFState& st) {
      const sim::dynamics::rigid_body::Vec3 v_app{st.velocity_body.x - latest_u_g_m_s_,
                                                  st.velocity_body.y - latest_v_g_m_s_,
                                                  st.velocity_body.z - latest_w_g_m_s_};
      const auto a = sim::aerodynamics::evaluateStabilityDerivative(
          aero_params_, v_app, st.angular_velocity_body, delta, rho);
      // Gyroscopic moment from spinning engine rotors:
      //   M_gyro = -ω_body × H_rotor
      // with engines aligned along body x, H_rotor = (H_total, 0, 0), so
      //   ω × (H, 0, 0) = (0, ω_z·H, -ω_y·H) = (0, r·H, -q·H)
      //   M_gyro = -(0, r·H, -q·H) = (0, -r·H, q·H)
      // → yaw rate creates pitch-down moment, pitch-up creates yaw-right moment.
      const double q_rate = st.angular_velocity_body.y;
      const double r_rate = st.angular_velocity_body.z;
      return sim::dynamics::rigid_body::Vec3{a.moment_body.x,
                                             a.moment_body.y - r_rate * H_rotor_total,
                                             a.moment_body.z + q_rate * H_rotor_total};
    };

    // 6. RK4 step. Mass + inertia come from fuel-burn (not the static `mass_kg`).
    sim::dynamics::rigid_body::stepRigidBody6DOF(rb_state_, force_fn, moment_fn, m_total_kg, I_now,
                                                 s.elapsed_s, DT_S);

    // 7. Reverse-project NED state → telemetry (lat/lon/alt + Euler).
    const double R_body = body_->telemetry().reference_radius_m;
    if (R_body > 0.0) {
      const double m_per_dlat = R_body * DEG_TO_RAD;
      const double m_per_dlon = R_body * std::cos(p.init_lat_deg * DEG_TO_RAD) * DEG_TO_RAD;
      if (m_per_dlat > 0.0) {
        tlm.pos_lat_deg = p.init_lat_deg + (rb_state_.position_inertial.x / m_per_dlat);
      }
      if (m_per_dlon > 0.0) {
        tlm.pos_lon_deg = p.init_lon_deg + (rb_state_.position_inertial.y / m_per_dlon);
      }
    }
    tlm.pos_alt_m = -rb_state_.position_inertial.z;

    // Attitude → roll/pitch/yaw (deg). toEuler returns {roll, pitch, yaw} ZYX.
    const auto eul = rb_state_.attitude.toEuler();
    tlm.roll_deg = eul[0] * RAD_TO_DEG;
    tlm.pitch_deg = eul[1] * RAD_TO_DEG;
    tlm.heading_deg = std::fmod(eul[2] * RAD_TO_DEG + 360.0, 360.0);

    // Velocity-derived: V (body-frame magnitude) + ground/vertical via inertial rotation.
    const double V = std::sqrt(rb_state_.velocity_body.x * rb_state_.velocity_body.x +
                               rb_state_.velocity_body.y * rb_state_.velocity_body.y +
                               rb_state_.velocity_body.z * rb_state_.velocity_body.z);
    const auto v_inertial = rb_state_.attitude.rotate(
        rb_state_.velocity_body.x, rb_state_.velocity_body.y, rb_state_.velocity_body.z);
    tlm.airspeed_m_s = V;
    tlm.groundspeed_m_s = std::sqrt(v_inertial[0] * v_inertial[0] + v_inertial[1] * v_inertial[1]);
    tlm.vertical_speed_m_s = -v_inertial[2]; // NED z = -alt; +vsp = climb
    if (V > 1.0) {
      tlm.alpha_rad = std::atan2(rb_state_.velocity_body.z, rb_state_.velocity_body.x);
      tlm.gamma_deg = std::asin(-v_inertial[2] / V) * RAD_TO_DEG;
    }

    // Re-evaluate aero at current state for telemetry forces (the lambda
    // evaluations during RK4 sampled stage points, not the final state).
    // Use apparent wind here too for consistency with what the integrator saw.
    const sim::dynamics::rigid_body::Vec3 v_app_post{rb_state_.velocity_body.x - latest_u_g_m_s_,
                                                     rb_state_.velocity_body.y - latest_v_g_m_s_,
                                                     rb_state_.velocity_body.z - latest_w_g_m_s_};
    const auto aero_out = sim::aerodynamics::evaluateStabilityDerivative(
        aero_params_, v_app_post, rb_state_.angular_velocity_body, delta, rho);
    tlm.dynamic_pressure_Pa = aero_out.q_Pa;
    tlm.lift_N = aero_out.L_N;
    tlm.drag_N = aero_out.D_N;
    tlm.thrust_N = T_total_N; // from Turbofan2Spool (per-tick spool state)
    tlm.mach = V / A_SL_M_S;
    tlm.rho_kg_m3 = rho;

    // Mass / fuel from FuelBurnMassProperties.
    tlm.mass_kg = m_total_kg;         // m_empty + fuel(t)
    tlm.fuel_kg = fuel_tank_.fuel_kg; // burns down at TSFC*T*dt
    tlm.throttle = throttle;
    tlm.bank_cmd_deg = bank_ref_rad * RAD_TO_DEG;
    tlm.pitch_cmd_deg = pitch_ref_rad * RAD_TO_DEG;
    tlm.altitude_AGL_m = 0.0; // populates from RadarAlt
    tlm.mode = controller_mode;
    tlm.engine_state = 0u; // populated when engine-state telemetry is wired

    // Body rates + control surfaces.
    tlm.p_rad_s = rb_state_.angular_velocity_body.x;
    tlm.q_rad_s = rb_state_.angular_velocity_body.y;
    tlm.r_rad_s = rb_state_.angular_velocity_body.z;
    tlm.elevator_rad = delta.elevator_rad;
    tlm.aileron_rad = delta.aileron_rad;
    tlm.rudder_rad = delta.rudder_rad;

    // Stamp simulated state time on the tick grid, anchored to the
    // monotonic clock once at the first published tick: state and
    // stamp agree exactly, so scheduler jitter never reaches the wire
    // and consumer-side interpolators see a clean timeline.
    if (t0_ns_ == 0u) {
      t0_ns_ = static_cast<std::uint64_t>(apex::helpers::cpu::getMonotonicNs());
      t0_tick_ = s.tick_count;
    }
    constexpr std::uint64_t DT_NS = static_cast<std::uint64_t>(DT_S * 1.0e9);
    tlm.timestamp_ns = t0_ns_ + (s.tick_count - t0_tick_) * DT_NS;
    tlm.tick = s.tick_count;
    ++s.tick_count;
    s.elapsed_s += DT_S;
    return 0u;
  }

  /// Periodic log line summarizing pose + airdata.
  std::uint8_t telemetryTick() noexcept {
    auto* log = componentLog();
    if (log == nullptr)
      return 0u;
    const auto& tlm = telemetry_.get();
    const auto& p = tunables_.get();
    log->info(label(), fmt::format("tick={} {} lat={:.5f} lon={:.5f} alt={:7.1f}m "
                                   "hdg={:6.2f} V={:6.1f}m/s IAS={:5.1f}m/s M={:.2f} γ={:5.2f}deg "
                                   "α={:5.3f}rad ρ={:.3f} L={:.0f}N D={:.0f}N T={:.0f}N "
                                   "fuel={:.0f}kg gust(u,v,w)=({:+5.2f},{:+5.2f},{:+5.2f})m/s",
                                   tlm.tick, p.body_label, tlm.pos_lat_deg, tlm.pos_lon_deg,
                                   tlm.pos_alt_m, tlm.heading_deg, tlm.airspeed_m_s,
                                   latest_IAS_m_s_, tlm.mach, tlm.gamma_deg, tlm.alpha_rad,
                                   tlm.rho_kg_m3, tlm.lift_N, tlm.drag_N, tlm.thrust_N, tlm.fuel_kg,
                                   latest_u_g_m_s_, latest_v_g_m_s_, latest_w_g_m_s_));
    return 0u;
  }

  /* ----------------------------- Command handling ----------------------------- */

  /// Dispatch component-specific opcodes (0x0100+); delegate everything
  /// else to SwModelBase. See AircraftCommand.hpp for the opcode list.
  ///
  /// Both transports (TCP/APROTO via ApexInterface, SHM/APROTO via
  /// the bridge command sink) land here through the same routing fabric.
  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override {
    using appsim::aircraft::AircraftCmdSetEnable;
    using appsim::aircraft::AircraftCommandSnapshot;
    using appsim::aircraft::AircraftOpcode;
    using system_core::system_component::CommandResult;

    switch (static_cast<AircraftOpcode>(opcode)) {
    case AircraftOpcode::SET_TURBULENCE_ENABLE: {
      if (payload.size() < sizeof(AircraftCmdSetEnable)) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
      }
      const auto& cmd = *reinterpret_cast<const AircraftCmdSetEnable*>(payload.data());
      const bool old = turbulence_enabled_;
      turbulence_enabled_ = (cmd.enabled != 0);
      ++cmd_count_;
      if (auto* log = componentLog(); log != nullptr) {
        log->info(label(), fmt::format("cmd: SET_TURBULENCE_ENABLE {} -> {}", old ? "ON" : "OFF",
                                       turbulence_enabled_ ? "ON" : "OFF"));
      }
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    case AircraftOpcode::SET_GUST_ALLEVIATION_ENABLE: {
      // Aircraft owns the runtime enable flag; AircraftController gates
      // its GustAlleviation feedforward on aircraft_->isGustAlleviationEnabled().
      // This keeps Aircraft as the single source of disturbance / response
      // command state and avoids a parallel command path to AircraftController.
      if (payload.size() < sizeof(AircraftCmdSetEnable)) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
      }
      const auto& cmd = *reinterpret_cast<const AircraftCmdSetEnable*>(payload.data());
      const bool old = gust_alleviation_enabled_;
      gust_alleviation_enabled_ = (cmd.enabled != 0);
      ++cmd_count_;
      if (auto* log = componentLog(); log != nullptr) {
        log->info(label(),
                  fmt::format("cmd: SET_GUST_ALLEVIATION_ENABLE {} -> {}", old ? "ON" : "OFF",
                              gust_alleviation_enabled_ ? "ON" : "OFF"));
      }
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    case AircraftOpcode::GET_COMMAND_STATE: {
      AircraftCommandSnapshot snap{};
      snap.turbulence_enabled = turbulence_enabled_ ? 1u : 0u;
      snap.gust_alleviation_enabled = gust_alleviation_enabled_ ? 1u : 0u;
      snap.cmd_count = cmd_count_;
      response.resize(sizeof(snap));
      std::memcpy(response.data(), &snap, sizeof(snap));
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    default:
      // Delegate base/system opcodes (GET_HEALTH, RELOAD_TPRM, etc.).
      return SwModelBase::handleCommand(opcode, payload, response);
    }
  }

protected:
  /* ----------------------------- Lifecycle ----------------------------- */

  /// Optional TPRM tunable load. Uses the typed-reject reader so a
  /// size or identity mismatch is a loud, classified fault instead of
  /// a silent fallback to defaults.
  [[nodiscard]] bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    const std::filesystem::path PATH = tprmDir / fmt::format("{:06x}.tprm", fullUid());
    std::error_code ec;
    if (!std::filesystem::exists(PATH, ec)) {
      return true; // Tunables stay at defaults; init can still proceed.
    }
    const auto CHECK =
        system_core::system_component::readTprmPayload(PATH, fullUid(), tunables_.get());
    if (CHECK != system_core::system_component::TprmPayloadCheck::OK) {
      auto* log = componentLog();
      if (log != nullptr) {
        log->error(label(), system_core::system_component::toFaultCode(CHECK),
                   fmt::format("TPRM rejected ({}): {}",
                               system_core::system_component::toString(CHECK), PATH.string()));
      }
      return false;
    }
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("loadTprm: tunables loaded from {}", PATH.string()));
    }
    return true;
  }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    // Step + telemetry share sequence group 0 (step phase 0,
    // telemetry phase 1) so a scheduler can run them race-free when
    // they land on the same tick; the TPRM task table opts in per
    // task.
    (void)createSequenceGroup(0, 2);
    registerSequencedTask<Aircraft, &Aircraft::aircraftStep>(
        static_cast<std::uint8_t>(TaskUid::AIRCRAFT_STEP), this, "aircraftStep", 0, 0);
    registerSequencedTask<Aircraft, &Aircraft::telemetryTick>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry", 0, 1);

    registerData(DataCategory::TUNABLE_PARAM, "tunables", &tunables_.get(),
                 sizeof(AircraftTunables));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(AircraftState));
    registerData(DataCategory::OUTPUT, "telemetry", &telemetry_.get(), sizeof(AircraftTelemetry));

    auto* log = componentLog();
    if (log != nullptr) {
      const auto& p = tunables_.get();
      log->info(label(), fmt::format("init: body={} init_pos=({:.4f}, {:.4f}, {:.0f}m) "
                                     "hdg={:.1f}deg V={:.0f}m/s mass={:.0f}kg "
                                     "S={:.0f}m^2 AR={:.1f} dt={:.0f}ms body_attached={}",
                                     p.body_label, p.init_lat_deg, p.init_lon_deg, p.init_alt_m,
                                     p.init_heading_deg, p.init_speed_m_s, p.mass_kg,
                                     p.wing_area_m2, p.aspect_ratio, DT_S * 1e3, body_ != nullptr));
    }
    return static_cast<std::uint8_t>(ApexStatus::SUCCESS);
  }

private:
  /// Play the armed excitation into `delta` for this tick and advance
  /// its clock; auto-clears when the script completes. SPEED_OFFSET
  /// mutates body velocity once instead of touching surfaces.
  void applyExcitation_(sim::aerodynamics::ControlInputs& delta) noexcept {
    switch (excite_mode_) {
    case ExciteMode::NONE:
      return;
    case ExciteMode::RUDDER_DOUBLET:
      if (excite_t_s_ < 0.5) {
        delta.rudder_rad += 0.05;
      } else if (excite_t_s_ < 1.0) {
        delta.rudder_rad -= 0.05;
      } else {
        excite_mode_ = ExciteMode::NONE;
      }
      break;
    case ExciteMode::ELEVATOR_PULSE:
      if (excite_t_s_ < 0.5) {
        delta.elevator_rad -= 0.05;
      } else {
        excite_mode_ = ExciteMode::NONE;
      }
      break;
    case ExciteMode::AILERON_PULSE:
      if (excite_t_s_ < 0.5) {
        delta.aileron_rad += 0.02;
      } else {
        excite_mode_ = ExciteMode::NONE;
      }
      break;
    case ExciteMode::SPEED_OFFSET:
      if (!excite_speed_applied_) {
        rb_state_.velocity_body.x += 10.0;
        excite_speed_applied_ = true;
      }
      excite_mode_ = ExciteMode::NONE;
      break;
    }
    excite_t_s_ += DT_S;
  }

  /// Seed `rb_state_`, `inertia_`, and `aero_params_` from tunables.
  /// Called once per run, on the first tick after the body becomes ready.
  /// The aero preset is the transport cruise set; geometry + inertia
  /// are overridden from tunables so per-aircraft scaling stays
  /// TPRM-driven.
  void initRigidBodyState_(const AircraftTunables& p) noexcept {
    inertia_.Ixx = p.I_xx_kgm2;
    inertia_.Iyy = p.I_yy_kgm2;
    inertia_.Izz = p.I_zz_kgm2;
    inertia_.Ixz = p.I_xz_kgm2;

    aero_params_ = sim::aerodynamics::transportCruisePreset();
    aero_params_.S_m2 = p.wing_area_m2;
    aero_params_.AR = p.aspect_ratio;
    aero_params_.e_oswald = p.oswald_e;

    // Initial attitude: ZYX intrinsic Euler (yaw → pitch → roll).
    using apex::math::integration::Quaternion;
    const double psi = p.init_heading_deg * DEG_TO_RAD;
    const double theta = p.init_pitch_deg * DEG_TO_RAD;
    const double phi = p.init_roll_deg * DEG_TO_RAD;
    const Quaternion q_yaw = Quaternion::fromAxisAngle(0.0, 0.0, 1.0, psi);
    const Quaternion q_pitch = Quaternion::fromAxisAngle(0.0, 1.0, 0.0, theta);
    const Quaternion q_roll = Quaternion::fromAxisAngle(1.0, 0.0, 0.0, phi);
    rb_state_.attitude = q_yaw * q_pitch * q_roll;

    // NED inertial origin at init pose (z = -altitude). lat/lon delta is
    // tracked relative to init via local-flat projection in telemetry.
    rb_state_.position_inertial = sim::dynamics::rigid_body::Vec3{0.0, 0.0, -p.init_alt_m};
    rb_state_.velocity_body = sim::dynamics::rigid_body::Vec3{p.init_speed_m_s, 0.0, 0.0};
    rb_state_.angular_velocity_body = sim::dynamics::rigid_body::Vec3{0.0, 0.0, 0.0};

    // Turbofan + fuel-burn state. Spools start at idle; the fuel tank
    // starts full per tunable. Dry/fuel inertia split by mass ratio,
    // both about the body reference point (no parallel-axis terms).
    turbofan_state_.N1_pct = turbofan_params_.N1_idle_pct;
    turbofan_state_.N2_pct = turbofan_params_.N2_idle_pct;
    const double m_full_kg = p.m_empty_kg + p.fuel_capacity_kg;
    const double DRY_FRAC = (m_full_kg > 0.0) ? p.m_empty_kg / m_full_kg : 1.0;
    inertia_dry_ =
        sim::dynamics::rigid_body::InertiaTensor{inertia_.Ixx * DRY_FRAC, inertia_.Iyy * DRY_FRAC,
                                                 inertia_.Izz * DRY_FRAC, inertia_.Ixz * DRY_FRAC};
    fuel_tank_.params.TSFC_kg_per_N_s = p.TSFC_kg_per_N_s;
    fuel_tank_.params.fuel_capacity_kg = p.fuel_capacity_kg;
    fuel_tank_.params.cg_tank_m = sim::dynamics::rigid_body::Vec3{0.0, 0.0, 0.0};
    fuel_tank_.params.I_fuel_full = sim::dynamics::rigid_body::InertiaTensor{
        inertia_.Ixx * (1.0 - DRY_FRAC), inertia_.Iyy * (1.0 - DRY_FRAC),
        inertia_.Izz * (1.0 - DRY_FRAC), inertia_.Ixz * (1.0 - DRY_FRAC)};
    fuel_tank_.fuel_kg = p.fuel_capacity_kg;
    fuel_tank_.fuel_burned_total_kg = 0.0;
  }

  const sim::environment::celestial_body::CelestialBody* body_{nullptr};
  const appsim::aircraft_controller::AircraftControllerOutput* controller_output_{nullptr};

  system_core::data::TunableParam<AircraftTunables> tunables_{};
  system_core::data::State<AircraftState> state_{};
  system_core::data::Output<AircraftTelemetry> telemetry_{};

  /* 6DOF cache: integrator state + inertia + aero preset. Set once
     in `initRigidBodyState_()` on the first tick; thereafter the
     integrator updates `rb_state_` in place each tick. */
  sim::dynamics::rigid_body::RigidBody6DOFState rb_state_{};
  sim::dynamics::rigid_body::InertiaTensor inertia_{};
  sim::aerodynamics::StabilityDerivativeAeroParams aero_params_{};

  /* Propulsion + mass-prop cache. Turbofan spools persist across
     ticks (first-order lag dynamics); fuel burns down over the run. */
  sim::propulsion::Turbofan2SpoolState turbofan_state_{};
  sim::propulsion::Turbofan2SpoolParams turbofan_params_{};
  sim::dynamics::mass_properties::FuelTankMassSource fuel_tank_{};
  sim::dynamics::rigid_body::InertiaTensor inertia_dry_{};

  /* disturbance + sensors. Dryden filter state persists across
     ticks (PSD-shaped white noise driven). Pitot is the only integrated
     sensor wired today; radar altimeter + GPS exist as library modules but
     don't drive demo behavior at 12 km cruise. */
  sim::dynamics::disturbance::DrydenTurbulenceState dryden_state_{};
  sim::dynamics::disturbance::DrydenTurbulenceParams dryden_params_{};
  mutable sim::dynamics::disturbance::DrydenRng dryden_rng_{}; // mutable: stepDryden modifies
  sim::sensors::Pitot pitot_{};
  // Cached for telemetry / controller-side queries:
  double latest_w_g_m_s_ = 0.0; ///< vertical gust (controller feedforward input)
  double latest_v_g_m_s_ = 0.0;
  double latest_u_g_m_s_ = 0.0;
  double latest_IAS_m_s_ = 0.0; ///< Pitot indicated airspeed

  /* ----------------------------- command state ----------------------------- */
  /// Runtime command flags mutated by handleCommand (TCP/APROTO via
  /// ApexInterface or SHM/APROTO via the bridge command sink). Defaults to
  /// existing pre-behavior so the sim remains binary-compatible
  /// when no commands have been sent.
  bool turbulence_enabled_ = true;
  bool gust_alleviation_enabled_ = true;
  std::uint64_t cmd_count_ = 0;

  /* Timestamp grid anchor: monotonic time of the first published tick
     and its tick number; stamps advance from there in exact DT steps. */
  std::uint64_t t0_ns_ = 0;
  std::uint64_t t0_tick_ = 0;

  /* Mode-excitation state: which script is armed and how far along. */
  ExciteMode excite_mode_ = ExciteMode::NONE;
  double excite_t_s_ = 0.0;
  bool excite_speed_applied_ = false;

public:
  // telemetry getters (consumed by AircraftController + telemetryTick log).
  double latestVerticalGust_m_s() const noexcept { return latest_w_g_m_s_; }
  double latestLateralGust_m_s() const noexcept { return latest_v_g_m_s_; }
  double latestLongGust_m_s() const noexcept { return latest_u_g_m_s_; }
  double latestIAS_m_s() const noexcept { return latest_IAS_m_s_; }
  // command-state getters (consumed by AircraftController).
  bool isTurbulenceEnabled() const noexcept { return turbulence_enabled_; }
  bool isGustAlleviationEnabled() const noexcept { return gust_alleviation_enabled_; }
};

} // namespace aircraft
} // namespace appsim

#endif // APEX_HORIZON_DEMO_AIRCRAFT_HPP
