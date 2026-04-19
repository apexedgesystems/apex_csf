#ifndef APEX_HIL_DEMO_PLANT_MODEL_HPP
#define APEX_HIL_DEMO_PLANT_MODEL_HPP
/**
 * @file HilPlantModel.hpp
 * @brief Schedulable plant model for the HIL flight demonstration.
 *
 * Wraps PointMassDynamics + DragModel into a SwModelBase subclass
 * with three scheduled tasks:
 *   - plantStep (100 Hz): Gravity + drag + thrust integration
 *   - control   (50 Hz):  PD altitude-hold controller (SIL mode)
 *   - telemetry (1 Hz):   Status logging
 *
 * All plant parameters (mass, drag, gains) are TPRM-configurable.
 *
 * @note NOT RT-safe: Uses double precision and fmt::print in telemetry.
 */

#include "apps/apex_hil_demo/model/inc/HilPlantData.hpp"
#include "apps/apex_hil_demo/model/inc/PointMassDynamics.hpp"
#include "apps/apex_hil_demo/model/inc/DragModel.hpp"

#include "src/system/core/infrastructure/system_component/apex/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <fmt/format.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace appsim {
namespace plant {

using system_core::data::Output;
using system_core::data::State;
using system_core::data::TunableParam;
using system_core::system_component::SwModelBase;

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Status codes for HilPlantModel.
 *
 * Extends system_component::Status::EOE_SYSTEM_COMPONENT.
 */
enum class Status : std::uint8_t {
  SUCCESS = 0,

  // Errors ------------------------------------------------------------------
  ERROR_COMM_LOSS =
      static_cast<std::uint8_t>(system_core::system_component::Status::EOE_SYSTEM_COMPONENT),

  // Marker ------------------------------------------------------------------
  EOE_HIL_PLANT_MODEL
};

/* ----------------------------- HilPlantModel ----------------------------- */

/**
 * @class HilPlantModel
 * @brief 3DOF plant simulation with PD controller.
 *
 * componentId = 120 (0x78)
 * fullUid = 0x7800 (single instance)
 *
 * @note NOT RT-safe: telemetry task uses fmt::print.
 */
class HilPlantModel final : public SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 120;
  static constexpr const char* COMPONENT_NAME = "HilPlantModel";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    PLANT_STEP = 1, ///< 100 Hz dynamics integration.
    CONTROL = 2,    ///< 50 Hz PD controller.
    TELEMETRY = 3   ///< 1 Hz status output.
  };

  /* ----------------------------- Construction ----------------------------- */

  HilPlantModel() noexcept = default;
  ~HilPlantModel() override = default;

  /* ----------------------------- Lifecycle ----------------------------- */

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    // Initialize plant from tunable params
    const auto& p = tunableParams_.get();
    dynamics_.init(p.mass);
    dynamics_.reset({0.0, 0.0, 0.0}, {0.0, 0.0, 0.0});
    drag_.init(p.dragCd, p.dragArea);

    // Register tasks
    registerTask<HilPlantModel, &HilPlantModel::plantStep>(
        static_cast<std::uint8_t>(TaskUid::PLANT_STEP), this, "plantStep");

    registerTask<HilPlantModel, &HilPlantModel::control>(
        static_cast<std::uint8_t>(TaskUid::CONTROL), this, "control");

    registerTask<HilPlantModel, &HilPlantModel::telemetry>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");

    // Register data for registry integration
    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &tunableParams_.get(),
                 sizeof(HilPlantTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(HilPlantState));
    registerData(DataCategory::OUTPUT, "vehicleState", &vehicleState_.get(),
                 sizeof(hil::VehicleState));

    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

public:
  /* ----------------------------- Task Methods ----------------------------- */

  /**
   * @brief Integrate dynamics one timestep (100 Hz).
   *
   * Computes gravity + drag + last controller thrust, advances state.
   */
  std::uint8_t plantStep() noexcept {
    constexpr double DT = 0.01; // 100 Hz

    // Gravity: constant NED (down = +z)
    const Vec3d GRAVITY = {0.0, 0.0, 9.80665};

    // Drag force from current state
    const Vec3d DRAG_FORCE = drag_.computeDrag(dynamics_.altitude(), dynamics_.velocity());

    // Propagate dynamics with last computed thrust
    dynamics_.step(DT, lastThrust_, GRAVITY, DRAG_FORCE);

    // Update state
    auto& s = state_.get();
    s.simTime += DT;
    s.lastAlt = dynamics_.altitude();
    s.lastVz = -dynamics_.velocity().z; // NED: -z is up
    ++s.stepCount;

    // Update published VehicleState output
    auto& vs = vehicleState_.get();
    const auto& POS = dynamics_.position();
    const auto& VEL = dynamics_.velocity();
    const auto& ACC = dynamics_.acceleration();
    vs.pos = {static_cast<float>(POS.x), static_cast<float>(POS.y), static_cast<float>(POS.z)};
    vs.vel = {static_cast<float>(VEL.x), static_cast<float>(VEL.y), static_cast<float>(VEL.z)};
    vs.accel = {static_cast<float>(ACC.x), static_cast<float>(ACC.y), static_cast<float>(ACC.z)};
    vs.altitude = static_cast<float>(s.lastAlt);
    vs.simTime = static_cast<float>(s.simTime);
    vs.stepCount = s.stepCount;

    return 0;
  }

  /**
   * @brief Apply thrust from external controller or internal PD (50 Hz).
   *
   * When an external control source is wired (via setControlSource), uses
   * the ControlCmd thrust vector directly. Falls back to the internal PD
   * altitude-hold controller when no external source is available.
   */
  std::uint8_t control() noexcept {
    auto& s = state_.get();
    const auto& p = tunableParams_.get();

    if (controlSource_ != nullptr) {
      // External controller path (SIL/HIL): use ControlCmd thrust directly.

      // Comm loss detection: track whether driver is receiving new commands.
      if (controlRxCount_ != nullptr && p.commLossFrames > 0.0) {
        const std::uint32_t CURRENT_RX = *controlRxCount_;
        if (CURRENT_RX != s.lastSeenRxCount) {
          // New command received -- reset stale counter, clear comm loss.
          s.lastSeenRxCount = CURRENT_RX;
          s.staleCmdFrames = 0;
          if (s.commLost != 0) {
            s.commLost = 0;
            setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
            setLastError(nullptr);
            auto* log = componentLog();
            if (log != nullptr) {
              log->info(label(), "Comm restored: receiving control commands");
            }
          }
        } else {
          ++s.staleCmdFrames;
          if (s.staleCmdFrames >= static_cast<std::uint32_t>(p.commLossFrames)) {
            if (s.commLost == 0) {
              s.commLost = 1;
              ++s.commLossCount;
              auto* log = componentLog();
              if (log != nullptr) {
                setStatus(static_cast<std::uint8_t>(Status::ERROR_COMM_LOSS));
                setLastError("COMM LOSS: zeroing thrust");
                log->error(label(), static_cast<std::uint8_t>(Status::ERROR_COMM_LOSS),
                           "Comm loss: zeroing thrust (failsafe)");
              }
            }
            // Failsafe: zero thrust on comm loss.
            lastThrust_ = {0.0, 0.0, 0.0};
            ++s.ctrlCount;
            return 0;
          }
        }
      }

      // ControlCmd thrust is in NED frame, matching plant conventions.
      const auto& CMD = *controlSource_;
      lastThrust_ = {static_cast<double>(CMD.thrust.x), static_cast<double>(CMD.thrust.y),
                     static_cast<double>(CMD.thrust.z)};
    } else {
      // Internal PD controller fallback (standalone mode).
      const double ALT = dynamics_.altitude();
      const double VZ = -dynamics_.velocity().z; // positive-up

      const double WEIGHT = p.mass * 9.80665;
      double thrustZ = p.ctrlKp * (p.targetAlt - ALT) - p.ctrlKd * VZ + WEIGHT;

      if (thrustZ > p.thrustMax) {
        thrustZ = p.thrustMax;
      }
      if (thrustZ < 0.0) {
        thrustZ = 0.0;
      }

      // NED: thrust up = negative z
      lastThrust_ = {0.0, 0.0, -thrustZ};
    }

    ++s.ctrlCount;
    return 0;
  }

  /**
   * @brief Log simulation status (1 Hz).
   */
  std::uint8_t telemetry() noexcept {
    const auto& s = state_.get();
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("t={:.1f}s alt={:.2f}m vz={:.2f}m/s steps={} commLoss={}",
                                     s.simTime, s.lastAlt, s.lastVz, s.stepCount, s.commLossCount));
    }
    ++state_.get().tlmCount;
    return 0;
  }

  [[nodiscard]] const char* label() const noexcept override { return "HIL_PLANT"; }

  /* ----------------------------- TPRM ----------------------------- */

  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    if (!isRegistered()) {
      return false;
    }

    char filename[32];
    std::snprintf(filename, sizeof(filename), "%06x.tprm", fullUid());
    std::filesystem::path tprmPath = tprmDir / filename;

    if (!std::filesystem::exists(tprmPath)) {
      logTprmConfig("defaults");
      return false;
    }

    std::string error;
    HilPlantTunableParams loaded{};
    if (apex::helpers::files::hex2cpp(tprmPath.string(), loaded, error)) {
      tunableParams_.get() = loaded;
      logTprmConfig(tprmPath.string());
      return true;
    }
    return false;
  }

  /* ----------------------------- Accessors ----------------------------- */

  [[nodiscard]] const HilPlantTunableParams& tunableParams() const noexcept {
    return tunableParams_.get();
  }
  [[nodiscard]] const HilPlantState& state() const noexcept { return state_.get(); }
  [[nodiscard]] const PointMassDynamics& dynamics() const noexcept { return dynamics_; }
  [[nodiscard]] const hil::VehicleState& vehicleState() const noexcept {
    return vehicleState_.get();
  }

  /**
   * @brief Set external control command source.
   * @param cmd Pointer to ControlCmd (must outlive this model). nullptr = use internal PD.
   */
  void setControlSource(const hil::ControlCmd* cmd) noexcept { controlSource_ = cmd; }

  /**
   * @brief Set pointer to driver's rxCount for comm loss detection.
   * @param rxCount Pointer to driver's receive counter (must outlive this model).
   *
   * When set, the plant tracks whether the rxCount is advancing. If it
   * stalls for commLossFrames consecutive control cycles, thrust is
   * zeroed as a failsafe. Without this pointer, comm loss detection
   * is disabled.
   *
   * @note RT-safe: Pointer assignment only.
   */
  void setControlCounter(const std::uint32_t* rxCount) noexcept { controlRxCount_ = rxCount; }

private:
  void logTprmConfig(const std::string& source) noexcept {
    auto* log = componentLog();
    if (log == nullptr) {
      return;
    }
    const auto& p = tunableParams_.get();
    log->info(label(), "=== TPRM Configuration ===");
    log->info(label(), fmt::format("Source: {}", source));
    log->info(label(),
              fmt::format("Plant: mass={}kg, Cd={}, A={}m^2", p.mass, p.dragCd, p.dragArea));
    log->info(label(), fmt::format("Controller: Kp={}, Kd={}, targetAlt={}m", p.ctrlKp, p.ctrlKd,
                                   p.targetAlt));
    log->info(label(), fmt::format("Limits: thrustMax={}N, commLossFrames={}", p.thrustMax,
                                   p.commLossFrames));
    log->info(label(), "==========================");
  }

  PointMassDynamics dynamics_;
  DragModel drag_;
  Vec3d lastThrust_{};
  const hil::ControlCmd* controlSource_ = nullptr;
  const std::uint32_t* controlRxCount_ = nullptr;
  TunableParam<HilPlantTunableParams> tunableParams_{};
  State<HilPlantState> state_{};
  Output<hil::VehicleState> vehicleState_{};
};

} // namespace plant
} // namespace appsim

#endif // APEX_HIL_DEMO_PLANT_MODEL_HPP
