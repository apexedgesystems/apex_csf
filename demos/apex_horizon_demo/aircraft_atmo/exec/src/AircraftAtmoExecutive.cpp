/**
 * @file AircraftAtmoExecutive.cpp
 * @brief Component registration + post-init smoke check for aircraft_atmo.
 */

#include "demos/apex_horizon_demo/aircraft_atmo/exec/inc/AircraftAtmoExecutive.hpp"

#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"

#include <cmath>
#include <fmt/format.h>

namespace appsim {
namespace aircraft_atmo {

/* ----------------------------- Bridge resolver ----------------------------- */

// Maps (fullUid, category) to the registered byte block via the registry,
// returning a read-only pointer: the bridge writes shm and never mutates
// the source.
static system_core::support::ResolvedSource
bridgeResolverFn(void* ctx, std::uint32_t fullUid,
                 system_core::data::DataCategory category) noexcept {
  auto* registry = static_cast<system_core::registry::ApexRegistry*>(ctx);
  auto* entry = registry->getData(fullUid, category);
  if (entry == nullptr || !entry->isValid()) {
    return {};
  }
  return {reinterpret_cast<const std::uint8_t*>(entry->dataPtr), entry->size};
}

/* ----------------------------- registerComponents ----------------------------- */

bool AircraftAtmoExecutive::registerComponents() noexcept {
  const auto& LOG_DIR = fileSystem().logDir();
  auto* log = sysLog();

  // Configuration values come from per-component .tprm files (loaded by
  // the framework between registration and init); the C++ here wires
  // cross-component pointers only.

  if (!registerComponent(&earth_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(earth) FAILED");
    return false;
  }

  aircraft_.setBody(&earth_);
  aircraft_.setControllerOutput(&controller_.controllerOutput());
  if (!registerComponent(&aircraft_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(aircraft) FAILED");
    return false;
  }

  controller_.setAircraft(&aircraft_);
  if (!registerComponent(&controller_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(controller) FAILED");
    return false;
  }

  // The shm publisher + command sink for the visualization consumer.
  // Registration failure is treated like any component failure; a
  // missing consumer at runtime is not a failure — the bridge idles.
  bridge_.setResolver(bridgeResolverFn, static_cast<void*>(&registry()));
  if (!registerComponent(&bridge_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(bridge) FAILED");
    return false;
  }

  if (log != nullptr) {
    log->info(label(), fmt::format("registered: earth_uid={:#x} aircraft_uid={:#x} "
                                   "controller_uid={:#x} bridge_uid={:#x}",
                                   earth_.fullUid(), aircraft_.fullUid(), controller_.fullUid(),
                                   bridge_.fullUid()));
  }
  return true;
}

/* ----------------------------- configureComponents ----------------------------- */

void AircraftAtmoExecutive::configureComponents() noexcept {
  auto* log = sysLog();
  if (log == nullptr) {
    return;
  }

  if (!earth_.isReady()) {
    log->info(label(), "smoke FAIL: earth not ready");
    return;
  }

  // Gravity at a 7000-km radial position (sanity on the J2 model).
  const double EARTH_R[3] = {7.0e6, 0.0, 0.0};
  double a[3] = {0.0, 0.0, 0.0};
  (void)earth_.gravity()->acceleration(EARTH_R, a);
  const double G_MAG = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);

  // Atmosphere density at the demo's cruise altitude and at sea level:
  // the pair pins the layered table's shape (rho decreasing with
  // altitude) before the aircraft flies it.
  double rho_sl = 0.0;
  (void)earth_.atmosphere()->density(0.0, 0.0, 0.0, rho_sl);
  double rho_cruise = 0.0;
  (void)earth_.atmosphere()->density(12192.0, 0.0, 0.0, rho_cruise);

  log->info(label(), "smoke-check: atmosphere-only world queries");
  log->info(label(), fmt::format("  earth: |g|={:.3f} m/s^2 @ 7000km   rho_sl={:.4f} kg/m^3   "
                                 "rho@12192m={:.4f} kg/m^3",
                                 G_MAG, rho_sl, rho_cruise));
}

} // namespace aircraft_atmo
} // namespace appsim
