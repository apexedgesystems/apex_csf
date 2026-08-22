#ifndef APEX_HORIZON_DEMO_WORLD_QUERY_PROBE_HPP
#define APEX_HORIZON_DEMO_WORLD_QUERY_PROBE_HPP
/**
 * @file WorldQueryProbe.hpp
 * @brief Demo schedulable apex component that exercises CelestialBody queries.
 *
 * An active `SwModelBase`-derived
 * component with one registered task (`probeTick`). Each tick:
 *   1. Picks the next survey point from its tunable list (round-robin).
 *   2. Queries the attached CelestialBody for terrain elevation and
 *      atmosphere density at that lat/lon.
 *   3. Queries gravity acceleration at a configurable radius.
 *   4. Logs the result to its component log.
 *
 * Body attachment is via a non-owning `CelestialBody*` pointer set by
 * the executive in `registerComponents()` before init. (Future versions
 * would resolve via the data registry by component fullUid; for the
 * demo a direct pointer is simpler and avoids registry plumbing.)
 *
 * Tasks:
 *   - probeTick (1 Hz, scheduled via TPRM): the work above.
 *
 * @note RT-safe queries; logging is NOT RT-safe (uses fmt::format).
 */

#include "demos/apex_horizon_demo/world_query_probe/inc/WorldQueryProbeData.hpp"

#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/atmosphere/inc/AtmosphereStatus.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"
#include "src/utilities/math/vecmat/inc/Angles.hpp"
#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"
#include "src/sim/environment/gravity/inc/GravityModelBase.hpp"
#include "src/sim/environment/terrain/inc/TerrainModelBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fmt/format.h>
#include <string>
#include <system_error>

namespace appsim {
namespace world_query_probe {

using ApexStatus = system_core::system_component::Status;

/* ----------------------------- WorldQueryProbe ----------------------------- */

class WorldQueryProbe final : public system_core::system_component::SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component class ID. Multiple probe instances share this; the
  /// executive assigns distinct full UIDs at registration.
  static constexpr std::uint16_t COMPONENT_ID = 221;
  static constexpr const char* COMPONENT_NAME = "WorldQueryProbe";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "WORLD_PROBE"; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    PROBE_TICK = 1, ///< Periodic probe (typically 1 Hz via TPRM).
  };

  /* ----------------------------- Construction ----------------------------- */

  WorldQueryProbe() noexcept = default;
  ~WorldQueryProbe() override = default;

  WorldQueryProbe(const WorldQueryProbe&) = delete;
  WorldQueryProbe& operator=(const WorldQueryProbe&) = delete;

  /* ----------------------------- Wiring ----------------------------- */

  /// Set the CelestialBody this probe queries. Pointer must outlive the
  /// probe. Call before `registerComponent` / `init`.
  void setBody(const sim::environment::celestial_body::CelestialBody* body) noexcept {
    body_ = body;
  }
  [[nodiscard]] const sim::environment::celestial_body::CelestialBody* body() const noexcept {
    return body_;
  }

  /* ----------------------------- Tunables / state accessors ----------------------------- */

  [[nodiscard]] system_core::data::TunableParam<WorldQueryProbeTunables>& tunables() noexcept {
    return tunables_;
  }
  [[nodiscard]] const WorldQueryProbeState& probeState() const noexcept { return state_.get(); }

  /// Most recent published OUTPUT snapshot. Updated each `probeTick`.
  [[nodiscard]] const WorldQueryProbeTelemetry& telemetry() const noexcept {
    return telemetry_.get();
  }

  /* ----------------------------- Task ----------------------------- */

  /// One probe iteration. Picks the next survey point, queries the
  /// attached body, updates the OUTPUT telemetry, logs the result.
  /// Returns 0 unconditionally so the scheduler keeps invoking us.
  std::uint8_t probeTick() noexcept {
    auto& s = state_.get();
    auto& tlm = telemetry_.get();
    const auto& p = tunables_.get();

    if (body_ == nullptr || !body_->isReady() || p.num_points == 0u) {
      ++s.consecutive_failures;
      tlm.last_query_succeeded = 0u;
      return 0u;
    }

    const std::uint32_t IDX = s.next_point_index % p.num_points;
    const double LAT_DEG = p.lat_deg[IDX];
    const double LON_DEG = p.lon_deg[IDX];
    const double LAT_RAD = LAT_DEG * apex::math::vecmat::DEG_TO_RAD;
    const double LON_RAD = LON_DEG * apex::math::vecmat::DEG_TO_RAD;

    bool ok = true;

    double H = 0.0;
    if (body_->terrain()->elevationAt(LAT_RAD, LON_RAD, H) !=
        sim::environment::terrain::Status::SUCCESS) {
      ok = false;
    }

    sim::environment::atmosphere::AtmosphereState astate{};
    const auto ATM_STATUS =
        body_->atmosphere()->query(p.atmosphere_alt_m, LAT_RAD, LON_RAD, astate);
    // Vacuum bodies legitimately answer WARN_VACUUM_QUERY with rho=0;
    // anything else non-SUCCESS is a real error.
    if (ATM_STATUS != sim::environment::atmosphere::Status::SUCCESS &&
        ATM_STATUS != sim::environment::atmosphere::Status::WARN_VACUUM_QUERY) {
      ok = false;
    }

    // Compose a "gravity sample point" in body-fixed coordinates: place
    // the spacecraft along +X at the requested radius.
    double radiusM = p.gravity_query_radius_m;
    if (radiusM <= 0.0) {
      // Default: ref_radius + atmosphere_alt isn't directly accessible
      // from the polymorphic gravity model. Use a hardcoded conservative
      // value (8000 km) so we still produce a useful number. Tunables
      // override if the caller wants a body-specific radius.
      radiusM = 8.0e6;
    }
    const double R[3] = {radiusM, 0.0, 0.0};
    double a[3] = {0.0, 0.0, 0.0};
    if (!body_->gravity()->acceleration(R, a)) {
      ok = false;
    }
    const double G_MAG = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);

    // Log (NOT RT-safe; cold path).
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("tick={} {} (lat={:7.3f} lon={:8.3f}) "
                                     "H={:8.1f}m  rho={:.4e}kg/m^3  |g|@{:.0f}km={:.4f} m/s^2",
                                     s.tick_count, p.body_label, LAT_DEG, LON_DEG, H, astate.rho,
                                     radiusM / 1000.0, G_MAG));
    }

    // Publish OUTPUT telemetry. This is the public face other components
    // (BridgeWriter, ground stations, system monitors) subscribe to.
    tlm.last_tick = s.tick_count;
    tlm.last_point_index = IDX;
    tlm.last_query_succeeded = ok ? 1u : 0u;
    tlm.last_lat_deg = LAT_DEG;
    tlm.last_lon_deg = LON_DEG;
    tlm.last_terrain_elevation_m = H;
    tlm.last_atmosphere_density_kg_m3 = astate.rho;
    tlm.last_atmosphere_pressure_Pa = astate.P;
    tlm.last_atmosphere_temperature_K = astate.T;
    tlm.last_atmosphere_sound_speed_m_s = astate.a;
    tlm.last_gravity_magnitude_m_s2 = G_MAG;

    if (ok) {
      s.consecutive_failures = 0u;
    } else {
      ++s.consecutive_failures;
    }
    s.next_point_index = (IDX + 1u) % p.num_points;
    ++s.tick_count;
    return 0u;
  }

protected:
  /* ----------------------------- Lifecycle ----------------------------- */

  /// Optional TPRM tunable load. Typed-reject reader: a size or
  /// identity mismatch is a loud classified fault, not a silent
  /// fallback to defaults.
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

    // Register the periodic task so the scheduler can find us by
    // (fullUid, taskUid). Frequency comes from the scheduler.tprm.
    registerTask<WorldQueryProbe, &WorldQueryProbe::probeTick>(
        static_cast<std::uint8_t>(TaskUid::PROBE_TICK), this, "probeTick");

    // Expose tunables + state + telemetry via the data registry.
    registerData(DataCategory::TUNABLE_PARAM, "tunables", &tunables_.get(),
                 sizeof(WorldQueryProbeTunables));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(WorldQueryProbeState));
    registerData(DataCategory::OUTPUT, "telemetry", &telemetry_.get(),
                 sizeof(WorldQueryProbeTelemetry));

    auto* log = componentLog();
    if (log != nullptr) {
      const auto& p = tunables_.get();
      log->info(label(), fmt::format("init: body_label={} num_points={} body_attached={}",
                                     p.body_label, p.num_points, body_ != nullptr));
    }
    return static_cast<std::uint8_t>(ApexStatus::SUCCESS);
  }

private:
  const sim::environment::celestial_body::CelestialBody* body_{nullptr};
  system_core::data::TunableParam<WorldQueryProbeTunables> tunables_{};
  system_core::data::State<WorldQueryProbeState> state_{};
  system_core::data::Output<WorldQueryProbeTelemetry> telemetry_{};
};

} // namespace world_query_probe
} // namespace appsim

#endif // APEX_HORIZON_DEMO_WORLD_QUERY_PROBE_HPP
