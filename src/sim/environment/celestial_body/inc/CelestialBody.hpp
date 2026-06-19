#ifndef APEX_SIM_ENVIRONMENT_CELESTIAL_BODY_HPP
#define APEX_SIM_ENVIRONMENT_CELESTIAL_BODY_HPP
/**
 * @file CelestialBody.hpp
 * @brief Apex SwModelBase wrapping a per-body environment model bundle.
 *
 * `CelestialBody` is a passive (no scheduled tasks) apex component that
 * represents one celestial body in a simulation -- Earth, Moon, Sun,
 * an asteroid, or a procedural fictional planet. It owns a bundle of
 * environment models (gravity / terrain / atmosphere) selected by
 * `CelestialBodyTunables`, exposes them via the data registry for
 * other components to query, and (optionally) loads any file-backed
 * data those models require.
 *
 * Lifecycle:
 *   1. Caller (typically the executive) constructs the component +
 *      sets tunables via `tunables().set(struct)`.
 *   2. Executive calls `registerComponent(&body, logDir)` which calls
 *      `init()` -> `doInit()` here.
 *   3. `doInit()` runs makeEnvironment(spec), loads any file-backed
 *      models, registers data with the apex registry, returns SUCCESS.
 *   4. After init, other components query `body.gravity()`, etc.
 *      Reads are RT-safe.
 *
 * No scheduled tasks. Other (active) components are expected to query
 * this one each tick as needed.
 *
 * @note RT-safe queries after init; init itself is NOT RT-safe (file I/O).
 */

#include "src/sim/environment/celestial_body/inc/CelestialBodyData.hpp"
#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFactory.hpp"
#include "src/sim/environment/gravity/inc/GravityModelBase.hpp"
#include "src/sim/environment/terrain/inc/TerrainModelBase.hpp"

#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include <cstdint>
#include <filesystem>

namespace sim {
namespace environment {
namespace celestial_body {

using ApexStatus = system_core::system_component::Status;

/* ----------------------------- CelestialBody ----------------------------- */

class CelestialBody final : public system_core::system_component::SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component class ID. Multiple CelestialBody *instances* (Earth, Moon, ...)
  /// share this class ID; instances are distinguished by per-instance UID
  /// assigned by the executive at registration time.
  static constexpr std::uint16_t COMPONENT_ID = 220;
  static constexpr const char* COMPONENT_NAME = "CelestialBody";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "CELESTIAL_BODY"; }

  /* ----------------------------- Construction ----------------------------- */

  CelestialBody() noexcept = default;
  ~CelestialBody() override = default;

  CelestialBody(const CelestialBody&) = delete;
  CelestialBody& operator=(const CelestialBody&) = delete;

  /* ----------------------------- Tunables / state accessors ----------------------------- */

  /// Mutable handle on the tunable parameters. Caller (executive) uses
  /// this to pre-populate body / fidelity / data path before calling
  /// `init()` (which is invoked by `registerComponent`).
  [[nodiscard]] system_core::data::TunableParam<CelestialBodyTunables>& tunables() noexcept {
    return tunables_;
  }
  [[nodiscard]] const system_core::data::TunableParam<CelestialBodyTunables>&
  tunables() const noexcept {
    return tunables_;
  }

  /// Internal lifecycle state (env_built / data_loaded / init_status).
  [[nodiscard]] const CelestialBodyState& bodyState() const noexcept { return state_.get(); }

  /// Public-face telemetry (OUTPUT). Populated once at init.
  [[nodiscard]] const CelestialBodyTelemetry& telemetry() const noexcept {
    return telemetry_.get();
  }

  /* ----------------------------- Environment query accessors ----------------------------- */

  /// Polymorphic gravity model. Valid only after a successful init().
  [[nodiscard]] const sim::environment::gravity::GravityModelBase* gravity() const noexcept {
    return env_.gravity.get();
  }

  /// Polymorphic terrain model. Valid only after a successful init().
  [[nodiscard]] const sim::environment::terrain::TerrainModelBase* terrain() const noexcept {
    return env_.terrain.get();
  }

  /// Polymorphic atmosphere model. Valid only after a successful init().
  [[nodiscard]] const sim::environment::atmosphere::AtmosphereModelBase*
  atmosphere() const noexcept {
    return env_.atmosphere.get();
  }

  /// Convenience: did init() complete and all queries are usable?
  [[nodiscard]] bool isReady() const noexcept { return state_.get().init_status == 1u; }

protected:
  /* ----------------------------- Lifecycle ----------------------------- */

  /// Optional: load tunables from a per-component `.tprm` file at
  /// `tprmDir/{fullUid:06x}.tprm`. Called by the apex executive between
  /// `initComponentLog()` and `init()`. If no file exists, the C++
  /// struct defaults (or any prior `tunables().set(...)` call) stand;
  /// the override returns true so the framework continues. Returns
  /// false only on a real I/O error or a size-mismatch read.
  [[nodiscard]] bool loadTprm(const std::filesystem::path& tprmDir) noexcept override;

  /// Build the env bundle from tunables; load any file-backed models.
  /// Registers tunables + state + telemetry with the data registry.
  /// No tasks (passive component).
  [[nodiscard]] std::uint8_t doInit() noexcept override;

private:
  system_core::data::TunableParam<CelestialBodyTunables> tunables_{};
  system_core::data::State<CelestialBodyState> state_{};
  system_core::data::Output<CelestialBodyTelemetry> telemetry_{};
  sim::environment::EnvironmentModels env_{};
};

} // namespace celestial_body
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_CELESTIAL_BODY_HPP
