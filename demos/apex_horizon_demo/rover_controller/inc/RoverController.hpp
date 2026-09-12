#ifndef APEX_HORIZON_DEMO_ROVER_CONTROLLER_HPP
#define APEX_HORIZON_DEMO_ROVER_CONTROLLER_HPP

/**
 * @file RoverController.hpp
 * @brief Drive controller for the demo rover: HOLD, TRAJECTORY, WAYPOINT.
 *
 * Reads the GroundVehicle's telemetry (pose on the body), works in a
 * local north/east grid about an anchor, and writes a drive command
 * (steer rate + throttle) the plant consumes. The wiring is direct
 * (`setVehicle`, and the plant is handed `driveCommand()`), as the
 * aircraft's controller is wired, so the same component runs on the
 * host in the software-in-the-loop form and its `controllerStep` is
 * the task the board firmware runs in the hardware form.
 *
 * WAYPOINT law: a proportional heading loop on the bearing error
 * (authority-limited), throttle at cruise while far, scaled down with
 * the remaining distance inside `slow_radius_m` so the first-order
 * plant settles inside `arrival_tolerance_m` without overshoot, and a
 * crawl throttle while the heading error is large so the rover turns
 * before it drives. Arrival latches `arrived` and zeroes the command
 * until a new target arrives.
 */

#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicle.hpp"
#include "demos/apex_horizon_demo/rover_controller/inc/RoverControllerData.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"
#include "src/utilities/math/vecmat/inc/Angles.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fmt/format.h>
#include <system_error>

namespace appsim {
namespace rover_controller {

using ApexStatus = system_core::system_component::Status;

static_assert(offsetof(RoverControllerOutput, steer_rate_deg_s) ==
                  offsetof(ground_vehicle::GroundVehicleDriveCommand, steer_rate_deg_s),
              "drive command head must alias GroundVehicleDriveCommand");
static_assert(offsetof(RoverControllerOutput, throttle_frac) ==
                  offsetof(ground_vehicle::GroundVehicleDriveCommand, throttle_frac),
              "drive command head must alias GroundVehicleDriveCommand");
static_assert(offsetof(RoverControllerOutput, valid) ==
                  offsetof(ground_vehicle::GroundVehicleDriveCommand, valid),
              "drive command head must alias GroundVehicleDriveCommand");

class RoverController final : public system_core::system_component::SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component class ID. Sequential after AircraftController (225).
  static constexpr std::uint16_t COMPONENT_ID = 226;
  static constexpr const char* COMPONENT_NAME = "RoverController";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "ROVER_CTL"; }

  enum class TaskUid : std::uint8_t {
    CONTROLLER_STEP = 1, ///< 10 Hz, the plant's cadence.
    TELEMETRY = 2,       ///< 1 Hz log line.
  };

  /* ----------------------------- Construction ----------------------------- */

  RoverController() noexcept = default;
  ~RoverController() override = default;
  RoverController(const RoverController&) = delete;
  RoverController& operator=(const RoverController&) = delete;

  /* ----------------------------- Wiring ----------------------------- */

  /// The plant this controller drives; must outlive the component.
  void setVehicle(const ground_vehicle::GroundVehicle* vehicle) noexcept { vehicle_ = vehicle; }
  [[nodiscard]] const ground_vehicle::GroundVehicle* vehicle() const noexcept { return vehicle_; }

  /// The block to hand the plant (`GroundVehicle::setDriveCommand`).
  [[nodiscard]] const ground_vehicle::GroundVehicleDriveCommand* driveCommand() const noexcept {
    return reinterpret_cast<const ground_vehicle::GroundVehicleDriveCommand*>(&output_.get());
  }

  [[nodiscard]] system_core::data::TunableParam<RoverControllerTunables>& tunables() noexcept {
    return tunables_;
  }
  [[nodiscard]] const RoverControllerState& controllerState() const noexcept {
    return state_.get();
  }
  [[nodiscard]] const RoverControllerOutput& controllerOutput() const noexcept {
    return output_.get();
  }

  /* ----------------------------- Mode and targets ----------------------------- */

  [[nodiscard]] DriveMode mode() const noexcept { return mode_; }
  void setMode(DriveMode mode) noexcept {
    mode_ = mode;
    if (mode != DriveMode::WAYPOINT) {
      output_.get().arrived = 0u;
    }
  }

  /// Target at (north, east) metres from the grid anchor.
  void setTargetAbs(double north_m, double east_m) noexcept {
    auto& s = state_.get();
    s.target_north_m = north_m;
    s.target_east_m = east_m;
    s.target_valid = 1u;
    ++s.target_seq;
    output_.get().arrived = 0u;
  }

  /// Target displaced (north, east) metres from where the rover is now.
  void setTargetRel(double north_m, double east_m) noexcept {
    double n = 0.0, e = 0.0;
    gridPosition(n, e);
    setTargetAbs(n + north_m, e + east_m);
  }

  /// The vehicle's position on the grid (0, 0 when no vehicle is attached).
  void gridPosition(double& north_m, double& east_m) const noexcept {
    north_m = 0.0;
    east_m = 0.0;
    if (vehicle_ == nullptr || vehicle_->body() == nullptr ||
        vehicle_->vehicleState().initialized == 0u) {
      return; // no pose yet: the plant latches its init pose on its first ready tick
    }
    const double R = vehicle_->body()->telemetry().reference_radius_m;
    if (R <= 0.0) {
      return;
    }
    const auto& p = tunables_.get();
    const auto& tlm = vehicle_->telemetry();
    // Same local-flat projection the plant integrates with, about the
    // anchor latitude.
    const double M_PER_DEG_LAT = R * apex::math::vecmat::DEG_TO_RAD;
    const double M_PER_DEG_LON = R * std::cos(p.anchor_lat_deg * apex::math::vecmat::DEG_TO_RAD) *
                                 apex::math::vecmat::DEG_TO_RAD;
    north_m = (tlm.pos_lat_deg - p.anchor_lat_deg) * M_PER_DEG_LAT;
    east_m = (tlm.pos_lon_deg - p.anchor_lon_deg) * M_PER_DEG_LON;
  }

  /* ----------------------------- Tasks ----------------------------- */

  /// One control step at the plant's cadence. Returns 0 unconditionally.
  std::uint8_t controllerStep() noexcept {
    auto& s = state_.get();
    auto& out = output_.get();
    const auto& p = tunables_.get();

    if (s.initialized == 0u) {
      mode_ = static_cast<DriveMode>(std::min<std::uint8_t>(p.boot_mode, 2u));
      s.initialized = 1u;
    }

    double n = 0.0, e = 0.0;
    gridPosition(n, e);
    out.grid_north_m = n;
    out.grid_east_m = e;
    out.mode = static_cast<std::uint8_t>(mode_);
    out.target_seq = s.target_seq;

    double steer = 0.0;
    double throttle = 0.0;
    switch (mode_) {
    case DriveMode::HOLD:
      break;
    case DriveMode::TRAJECTORY:
      steer = p.trajectory_steer_rate_deg_s;
      throttle = p.trajectory_throttle_frac;
      break;
    case DriveMode::WAYPOINT:
      waypointLaw(s, out, p, n, e, steer, throttle);
      break;
    }

    out.steer_rate_deg_s = steer;
    out.throttle_frac = std::clamp(throttle, 0.0, 1.0);
    // The block drives from the first tick (a HOLD boot must never let
    // the plant take a trajectory step); before the plant has latched
    // its pose the grid reads as the anchor, so a relative target set
    // that early is relative to the anchor.
    out.valid = (vehicle_ != nullptr) ? 1u : 0u;
    out.tick = s.tick_count;
    ++s.tick_count;
    return 0u;
  }

  std::uint8_t telemetryTick() noexcept {
    auto* log = componentLog();
    if (log == nullptr) {
      return 0u;
    }
    const auto& out = output_.get();
    const auto& s = state_.get();
    log->info(label(),
              fmt::format("tick={} mode={} grid=({:+.1f}N,{:+.1f}E) target=({:+.1f}N,{:+.1f}E)"
                          " valid={} seq={} dist={:.2f}m err={:+.1f}deg arrived={} "
                          "cmd: steer={:+.1f}deg/s thr={:.2f}",
                          out.tick, out.mode, out.grid_north_m, out.grid_east_m, s.target_north_m,
                          s.target_east_m, s.target_valid, s.target_seq, out.distance_m,
                          out.heading_error_deg, out.arrived, out.steer_rate_deg_s,
                          out.throttle_frac));
    return 0u;
  }

protected:
  [[nodiscard]] system_core::system_component::TprmIngest
  loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    using system_core::system_component::TprmIngest;
    const std::filesystem::path PATH = tprmDir / fmt::format("{:06x}.tprm", fullUid());
    std::error_code ec;
    if (!std::filesystem::exists(PATH, ec)) {
      return TprmIngest::DEFAULTS;
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

  [[nodiscard]] bool paramsOptional() const noexcept override { return true; }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;
    registerTask<RoverController, &RoverController::controllerStep>(
        static_cast<std::uint8_t>(TaskUid::CONTROLLER_STEP), this, "controllerStep");
    registerTask<RoverController, &RoverController::telemetryTick>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");
    registerData(DataCategory::TUNABLE_PARAM, "tunables", &tunables_.get(),
                 sizeof(RoverControllerTunables));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(RoverControllerState));
    registerData(DataCategory::OUTPUT, "output", &output_.get(), sizeof(RoverControllerOutput));
    auto* log = componentLog();
    if (log != nullptr) {
      const auto& p = tunables_.get();
      log->info(label(), fmt::format("init: boot_mode={} anchor=({:.4f}, {:.4f}) gain={:.2f} "
                                     "cruise={:.2f} approach={:.2f}/s tol={:.2f}m "
                                     "vehicle_attached={}",
                                     p.boot_mode, p.anchor_lat_deg, p.anchor_lon_deg,
                                     p.heading_gain, p.cruise_throttle_frac, p.approach_gain_per_s,
                                     p.arrival_tolerance_m, vehicle_ != nullptr));
    }
    return static_cast<std::uint8_t>(ApexStatus::SUCCESS);
  }

private:
  /// Wrap an angle difference to [-180, 180) degrees.
  static double wrap180(double deg) noexcept {
    double d = std::fmod(deg + 180.0, 360.0);
    if (d < 0.0) {
      d += 360.0;
    }
    return d - 180.0;
  }

  void waypointLaw(const RoverControllerState& s, RoverControllerOutput& out,
                   const RoverControllerTunables& p, double n, double e, double& steer,
                   double& throttle) noexcept {
    if (s.target_valid == 0u || vehicle_ == nullptr) {
      out.distance_m = 0.0;
      out.heading_error_deg = 0.0;
      return;
    }
    const double DN = s.target_north_m - n;
    const double DE = s.target_east_m - e;
    const double DIST = std::sqrt(DN * DN + DE * DE);
    out.distance_m = DIST;
    if (DIST < p.arrival_tolerance_m) {
      out.arrived = 1u;
    }
    if (out.arrived != 0u) {
      // Latched until a new target: the plant coasts to rest.
      out.bearing_deg =
          std::fmod(std::atan2(DE, DN) * apex::math::vecmat::RAD_TO_DEG + 360.0, 360.0);
      out.heading_error_deg = 0.0;
      return;
    }
    const double BEARING =
        std::fmod(std::atan2(DE, DN) * apex::math::vecmat::RAD_TO_DEG + 360.0, 360.0);
    const double ERR = wrap180(BEARING - vehicle_->telemetry().heading_deg);
    out.bearing_deg = BEARING;
    out.heading_error_deg = ERR;
    steer = std::clamp(p.heading_gain * ERR, -p.max_steer_rate_deg_s, p.max_steer_rate_deg_s);

    // Speed target: cruise while far, proportional to the remaining
    // distance as the leg closes, a crawl while still turning.
    const double MAX_V = vehicle_->tunables_const().max_speed_m_s;
    double frac = p.cruise_throttle_frac;
    if (MAX_V > 0.0) {
      const double V_APPROACH = p.approach_gain_per_s * DIST;
      frac = std::min(p.cruise_throttle_frac, V_APPROACH / MAX_V);
    }
    if (std::fabs(ERR) > p.turn_in_place_deg) {
      frac = std::min(frac, p.turn_throttle_frac);
    }
    throttle = frac;
  }

  const ground_vehicle::GroundVehicle* vehicle_{nullptr};
  DriveMode mode_{DriveMode::HOLD};
  system_core::data::TunableParam<RoverControllerTunables> tunables_{};
  system_core::data::State<RoverControllerState> state_{};
  system_core::data::Output<RoverControllerOutput> output_{};
};

} // namespace rover_controller
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_CONTROLLER_HPP
