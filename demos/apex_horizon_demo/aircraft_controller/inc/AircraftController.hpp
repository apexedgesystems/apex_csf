#ifndef APEX_HORIZON_DEMO_AIRCRAFT_CONTROLLER_HPP
#define APEX_HORIZON_DEMO_AIRCRAFT_CONTROLLER_HPP
/**
 * @file AircraftController.hpp
 * @brief Autopilot SW model. Composes the six named classical loops
 *        (PitchAttitudeHold, AltitudeHold, SpeedHold, RollController,
 *        HeadingHold, YawDamper) into a single apex SwModelBase.
 *
 * Tick layout (25 Hz inner cycle, 50 Hz Aircraft):
 *
 *   1. Read Aircraft's pose + body rates via direct pointer (set by the
 *      executive at registration time).
 *   2. AltitudeHold (h → θ_ref) → PitchAttitudeHold (θ_ref → δe)
 *      HeadingHold  (ψ → φ_ref) → RollController   (φ_ref → δa)
 *      YawDamper                                    (r     → δr)
 *      SpeedHold                                    (V     → throttle)
 *   3. Write the four control commands + diagnostics into the OUTPUT.
 *
 * The wiring to Aircraft is via `setAircraft(Aircraft*)` (direct pointer,
 * mirroring the `setBody(CelestialBody*)` pattern). Aircraft reads
 * controls back via `setController(AircraftController*)`. Both pointers
 * outlive the components.
 *
 * @note RT-safe within tick (no allocation); logging is NOT RT-safe.
 */

#include "demos/apex_horizon_demo/aircraft_controller/inc/AircraftControllerData.hpp"
#include "demos/apex_horizon_demo/aircraft/inc/Aircraft.hpp"

#include "src/sim/gnc/aircraft/inc/GustAlleviation.hpp"
#include "src/sim/gnc/aircraft/inc/LongitudinalControllers.hpp"
#include "src/sim/gnc/aircraft/inc/LateralControllers.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"
#include "src/utilities/helpers/inc/Cpu.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fmt/format.h>
#include <string>
#include <system_error>

namespace appsim {
namespace aircraft_controller {

using ApexStatus = system_core::system_component::Status;

/* --------------------------- Constants --------------------------- */

namespace {
constexpr double DEG_TO_RAD = 0.017453292519943295;
constexpr double DT_S = 1.0 / 25.0; ///< Controller tick at 25 Hz (half the aircraft rate).
} // namespace

/* --------------------------- AircraftController --------------------------- */

class AircraftController final : public system_core::system_component::SwModelBase {
public:
  /* --------------------------- Component Identity --------------------------- */

  /// Component class ID — sequential after Aircraft (224).
  static constexpr std::uint16_t COMPONENT_ID = 225;
  static constexpr const char* COMPONENT_NAME = "AircraftController";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "AIRCRAFT_CTL"; }

  /* --------------------------- Task UIDs --------------------------- */

  enum class TaskUid : std::uint8_t {
    CONTROLLER_STEP = 1, ///< Periodic control-law tick (25 Hz).
    TELEMETRY = 2,       ///< Periodic log line (1 Hz).
  };

  /* --------------------------- Per-loop enables --------------------------- */

  /// Bit assignments for the per-loop enable mask. A disabled loop
  /// contributes its neutral output (zero surface / trim throttle)
  /// while its PID state stops advancing — exactly what the
  /// dampers-off mode demonstrations need. Wire exposure (an ACFT/2
  /// SET_LOOP_ENABLE opcode) carries this same mask.
  enum LoopBit : std::uint8_t {
    LOOP_PITCH = 1u << 0,
    LOOP_ALT = 1u << 1,
    LOOP_SPEED = 1u << 2,
    LOOP_ROLL = 1u << 3,
    LOOP_HEADING = 1u << 4,
    LOOP_YAW_DAMPER = 1u << 5,
    LOOP_ALL = 0x3Fu,
  };

  void setLoopEnableMask(std::uint8_t mask) noexcept { loop_enable_mask_ = mask; }
  [[nodiscard]] std::uint8_t loopEnableMask() const noexcept { return loop_enable_mask_; }

  /* --------------------------- Construction --------------------------- */

  AircraftController() noexcept = default;
  ~AircraftController() override = default;

  AircraftController(const AircraftController&) = delete;
  AircraftController& operator=(const AircraftController&) = delete;

  /* --------------------------- Wiring --------------------------- */

  /// Set the Aircraft this controller drives. Pointer must outlive the
  /// component. Call before init.
  void setAircraft(const appsim::aircraft::Aircraft* aircraft) noexcept { aircraft_ = aircraft; }
  [[nodiscard]] const appsim::aircraft::Aircraft* aircraft() const noexcept { return aircraft_; }

  /* --------------------------- Tunables / state accessors --------------------------- */

  [[nodiscard]] system_core::data::TunableParam<AircraftControllerTunables>& tunables() noexcept {
    return tunables_;
  }
  [[nodiscard]] const AircraftControllerState& controllerState() const noexcept {
    return state_.get();
  }
  [[nodiscard]] const AircraftControllerOutput& controllerOutput() const noexcept {
    return output_.get();
  }

  /* --------------------------- Tasks --------------------------- */

  /// One control-law step. Returns 0 unconditionally.
  std::uint8_t controllerStep() noexcept {
    auto& s = state_.get();
    auto& out = output_.get();
    const auto& p = tunables_.get();

    // First tick: push tunable gains into the named loops.
    if (s.initialized == 0u) {
      pitch_loop_.setGains({p.pitch_Kp, p.pitch_Ki, p.pitch_Kd});
      pitch_loop_.setElevatorLimit(p.elevator_limit_rad);

      alt_loop_.setGains({p.alt_Kp, p.alt_Ki, p.alt_Kd});
      alt_loop_.setPitchLimit(p.pitch_ref_limit_rad);

      speed_loop_.setGains({p.speed_Kp, p.speed_Ki, p.speed_Kd});
      speed_loop_.setTrimThrottle(p.trim_throttle);

      roll_loop_.setGains({p.roll_Kp, p.roll_Ki, p.roll_Kd});
      roll_loop_.setAileronLimit(p.aileron_limit_rad);

      hdg_loop_.setGains({p.hdg_Kp, p.hdg_Ki, p.hdg_Kd});
      hdg_loop_.setBankLimit(p.bank_limit_rad);

      yaw_damper_.setGain(p.yaw_Kr);
      yaw_damper_.setRudderLimit(p.rudder_limit_rad);
      yaw_damper_.setWashoutTau(p.yaw_tau_w_s);

      // Gust-alleviation feedforward (longitudinal form). Elevator
      // limit comes from PitchAttitudeHold's clamp.
      sim::gnc::aircraft::GustAlleviationParams gp;
      gp.K_alpha_over_delta = p.gust_K_alpha_over_delta;
      gp.gust_authority_pct = p.gust_authority_pct;
      gp.elevator_limit_rad = p.elevator_limit_rad;
      gust_alleviation_.setParams(gp);

      s.initialized = 1u;
    }

    // Pass-through mode: emit zero controls. The aircraft falls back
    // to its open-loop defaults when controls = 0.
    if (p.enable_mode == 0u || aircraft_ == nullptr) {
      out.elevator_rad = 0.0;
      out.aileron_rad = 0.0;
      out.rudder_rad = 0.0;
      out.throttle = p.trim_throttle;
      out.mode = 0u;
      out.timestamp_ns = static_cast<std::uint64_t>(apex::helpers::cpu::getMonotonicNs());
      out.tick = s.tick_count++;
      return 0u;
    }

    // Closed-loop mode: read aircraft pose + body rates.
    const auto& tlm = aircraft_->telemetry();
    const double pitch_rad = tlm.pitch_deg * DEG_TO_RAD;
    const double roll_rad = tlm.roll_deg * DEG_TO_RAD;
    const double heading_rad = tlm.heading_deg * DEG_TO_RAD;
    const double altitude_m = tlm.pos_alt_m;
    const double airspeed_ms = tlm.airspeed_m_s;

    // Body yaw rate from the 6DOF integrator; the yaw damper uses r
    // feedback (washed out) to suppress Dutch roll.
    const double yaw_rate_rad_s = tlm.r_rad_s;

    // ---- Cascade: outer altitude → inner pitch ----
    // A disabled outer loop feeds a level (zero) reference to an
    // enabled inner loop; a disabled inner loop zeroes its surface.
    const std::uint8_t MASK = loop_enable_mask_;
    const double theta_ref =
        (MASK & LOOP_ALT) ? alt_loop_.step(p.target_altitude_m, altitude_m, DT_S) : 0.0;
    const double elevator_pid =
        (MASK & LOOP_PITCH) ? pitch_loop_.step(theta_ref, pitch_rad, DT_S) : 0.0;

    // ---- Gust feedforward: add δe_gust to PitchAttitudeHold output ----
    // Aircraft exposes the most recent vertical gust via getter. The
    // SET_GUST_ALLEVIATION_ENABLE command gates the contribution
    // at this site so a HUD toggle takes effect on the next controller
    // tick. Disabled → δe_gust = 0; gust_alleviation_.step() is still
    // called to keep any future internal-state evolution consistent.
    const double w_g = aircraft_->latestVerticalGust_m_s();
    const double elevator_gust_raw = gust_alleviation_.step(w_g, airspeed_ms);
    const double elevator_gust = aircraft_->isGustAlleviationEnabled() ? elevator_gust_raw : 0.0;
    double elevator = elevator_pid + elevator_gust;
    if (elevator > p.elevator_limit_rad)
      elevator = p.elevator_limit_rad;
    if (elevator < -p.elevator_limit_rad)
      elevator = -p.elevator_limit_rad;

    // ---- Cascade: outer heading → inner roll ----
    const double phi_ref =
        (MASK & LOOP_HEADING) ? hdg_loop_.step(p.target_heading_deg * DEG_TO_RAD, heading_rad, DT_S)
                              : 0.0;
    const double aileron = (MASK & LOOP_ROLL) ? roll_loop_.step(phi_ref, roll_rad, DT_S) : 0.0;

    // ---- Yaw damper (independent regulator) ----
    const double rudder = (MASK & LOOP_YAW_DAMPER) ? yaw_damper_.step(yaw_rate_rad_s, DT_S) : 0.0;

    // ---- Throttle from speed hold ----
    const double throttle = (MASK & LOOP_SPEED)
                                ? speed_loop_.step(p.target_airspeed_m_s, airspeed_ms, DT_S)
                                : p.trim_throttle;

    // ---- Pack output ----
    out.elevator_rad = elevator;
    out.aileron_rad = aileron;
    out.rudder_rad = rudder;
    out.throttle = throttle;
    out.pitch_ref_rad = theta_ref;
    out.bank_ref_rad = phi_ref;
    out.altitude_error_m = p.target_altitude_m - altitude_m;
    out.airspeed_error_m_s = p.target_airspeed_m_s - airspeed_ms;
    {
      double dpsi = (p.target_heading_deg * DEG_TO_RAD) - heading_rad;
      constexpr double kPi = 3.14159265358979323846;
      while (dpsi > kPi)
        dpsi -= 2.0 * kPi;
      while (dpsi < -kPi)
        dpsi += 2.0 * kPi;
      out.heading_error_rad = dpsi;
    }
    out.mode = 1u;
    out.timestamp_ns = static_cast<std::uint64_t>(apex::helpers::cpu::getMonotonicNs());
    out.tick = s.tick_count++;
    s.elapsed_s += DT_S;
    return 0u;
  }

  /// Periodic log line summarizing reference tracking.
  std::uint8_t telemetryTick() noexcept {
    auto* log = componentLog();
    if (log == nullptr)
      return 0u;
    const auto& out = output_.get();
    const auto& p = tunables_.get();
    log->info(label(), fmt::format("tick={} mode={} h_err={:+7.1f}m V_err={:+5.1f}m/s "
                                   "ψ_err={:+5.2f}rad θ_ref={:+5.3f}rad φ_ref={:+5.3f}rad "
                                   "δe={:+5.3f} δa={:+5.3f} δr={:+5.3f} thr={:.2f}",
                                   out.tick, static_cast<int>(out.mode), out.altitude_error_m,
                                   out.airspeed_error_m_s, out.heading_error_rad, out.pitch_ref_rad,
                                   out.bank_ref_rad, out.elevator_rad, out.aileron_rad,
                                   out.rudder_rad, out.throttle));
    (void)p;
    return 0u;
  }

protected:
  /* --------------------------- Lifecycle --------------------------- */

  /// Optional TPRM tunable load. Typed-reject reader: a size or
  /// identity mismatch is a loud classified fault, not a silent
  /// fallback to defaults.
  [[nodiscard]] system_core::system_component::TprmIngest
  loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    using system_core::system_component::TprmIngest;
    const std::filesystem::path PATH = tprmDir / fmt::format("{:06x}.tprm", fullUid());
    std::error_code ec;
    if (!std::filesystem::exists(PATH, ec)) {
      return TprmIngest::DEFAULTS; // Tunables stay at defaults; init can still proceed.
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
      return TprmIngest::REJECTED;
    }
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("loadTprm: tunables loaded from {}", PATH.string()));
    }
    return TprmIngest::LOADED;
  }

  /** @brief Defaults are a designed configuration for this demo component. */
  [[nodiscard]] bool paramsOptional() const noexcept override { return true; }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    // Step + telemetry share sequence group 0 (step phase 0,
    // telemetry phase 1); the TPRM task table opts in per task.
    (void)createSequenceGroup(0, 2);
    registerSequencedTask<AircraftController, &AircraftController::controllerStep>(
        static_cast<std::uint8_t>(TaskUid::CONTROLLER_STEP), this, "controllerStep", 0, 0);
    registerSequencedTask<AircraftController, &AircraftController::telemetryTick>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry", 0, 1);

    registerData(DataCategory::TUNABLE_PARAM, "tunables", &tunables_.get(),
                 sizeof(AircraftControllerTunables));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(AircraftControllerState));
    registerData(DataCategory::OUTPUT, "output", &output_.get(), sizeof(AircraftControllerOutput));

    auto* log = componentLog();
    if (log != nullptr) {
      const auto& p = tunables_.get();
      log->info(label(),
                fmt::format("init: target_alt={}m target_V={}m/s target_hdg={}deg "
                            "mode={} aircraft_attached={}",
                            p.target_altitude_m, p.target_airspeed_m_s, p.target_heading_deg,
                            static_cast<int>(p.enable_mode), aircraft_ != nullptr));
    }
    return static_cast<std::uint8_t>(ApexStatus::SUCCESS);
  }

private:
  const appsim::aircraft::Aircraft* aircraft_{nullptr};

  /// Per-loop enable mask (LoopBit); defaults to all loops active.
  std::uint8_t loop_enable_mask_ = LOOP_ALL;

  /* TPRM data triple */
  system_core::data::TunableParam<AircraftControllerTunables> tunables_{};
  system_core::data::State<AircraftControllerState> state_{};
  system_core::data::Output<AircraftControllerOutput> output_{};

  /* Six named classical loops */
  sim::gnc::aircraft::PitchAttitudeHold pitch_loop_;
  sim::gnc::aircraft::AltitudeHold alt_loop_;
  sim::gnc::aircraft::SpeedHold speed_loop_;
  sim::gnc::aircraft::RollController roll_loop_;
  sim::gnc::aircraft::HeadingHold hdg_loop_;
  sim::gnc::aircraft::YawDamper yaw_damper_;

  /* Gust-alleviation feedforward (longitudinal form). */
  sim::gnc::aircraft::GustAlleviation gust_alleviation_;
};

} // namespace aircraft_controller
} // namespace appsim

#endif // APEX_HORIZON_DEMO_AIRCRAFT_CONTROLLER_HPP
