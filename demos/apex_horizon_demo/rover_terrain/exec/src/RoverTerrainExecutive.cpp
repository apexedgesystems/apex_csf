/**
 * @file RoverTerrainExecutive.cpp
 * @brief Component registration + post-init smoke check for rover_terrain.
 */

#include "demos/apex_horizon_demo/rover_terrain/exec/inc/RoverTerrainExecutive.hpp"

#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"
#include "src/utilities/math/celestial/inc/EarthConstants.hpp"
#include "src/utilities/math/celestial/inc/MoonConstants.hpp"

#include <cmath>
#include <fmt/format.h>

namespace appsim {
namespace rover_terrain {

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

bool RoverTerrainExecutive::registerComponents() noexcept {
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

  if (!registerComponent(&moon_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(moon) FAILED");
    return false;
  }

  earthProbe_.setBody(&earth_);
  if (!registerComponent(&earthProbe_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(earthProbe) FAILED");
    return false;
  }

  moonProbe_.setBody(&moon_);
  if (!registerComponent(&moonProbe_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(moonProbe) FAILED");
    return false;
  }

  earthRover_.setBody(&earth_);
  if (!registerComponent(&earthRover_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(earthRover) FAILED");
    return false;
  }

  // The shm publisher for the visualization consumer. Registration
  // failure is treated like any component failure; a missing consumer
  // at runtime is not a failure — the bridge idles.
  bridge_.setResolver(bridgeResolverFn, static_cast<void*>(&registry()));
  if (!registerComponent(&bridge_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(bridge) FAILED");
    return false;
  }

  if (log != nullptr) {
    log->info(label(), fmt::format("registered: earth_uid={:#x} moon_uid={:#x} "
                                   "earthProbe_uid={:#x} moonProbe_uid={:#x} "
                                   "earthRover_uid={:#x} bridge_uid={:#x}",
                                   earth_.fullUid(), moon_.fullUid(), earthProbe_.fullUid(),
                                   moonProbe_.fullUid(), earthRover_.fullUid(), bridge_.fullUid()));
  }
  return true;
}

/* ----------------------------- configureComponents ----------------------------- */

void RoverTerrainExecutive::configureComponents() noexcept {
  namespace cel = apex::math::celestial;

  auto* log = sysLog();
  if (log == nullptr) {
    return;
  }

  if (!earth_.isReady() || !moon_.isReady()) {
    log->info(label(), fmt::format("smoke FAIL: earth_ready={} moon_ready={}", earth_.isReady(),
                                   moon_.isReady()));
    return;
  }

  // Earth gravity at a 7000-km radial position (low-orbit altitude).
  const double EARTH_R[3] = {7.0e6, 0.0, 0.0};
  double earthA[3] = {0.0, 0.0, 0.0};
  (void)earth_.gravity()->acceleration(EARTH_R, earthA);
  const double EARTH_G_MAG =
      std::sqrt(earthA[0] * earthA[0] + earthA[1] * earthA[1] + earthA[2] * earthA[2]);

  // Earth atmosphere density at sea level.
  double earthRho = 0.0;
  (void)earth_.atmosphere()->density(0.0, 0.0, 0.0, earthRho);

  // Earth terrain elevation at the equatorial surface point.
  const double EARTH_SURF[3] = {cel::earth::A, 0.0, 0.0};
  double earthH = 0.0;
  (void)earth_.terrain()->elevationAtEcef(EARTH_SURF, earthH);

  // Moon gravity at a 2000-km radial position (low-lunar-orbit altitude).
  const double MOON_R[3] = {2.0e6, 0.0, 0.0};
  double moonA[3] = {0.0, 0.0, 0.0};
  (void)moon_.gravity()->acceleration(MOON_R, moonA);
  const double MOON_G_MAG =
      std::sqrt(moonA[0] * moonA[0] + moonA[1] * moonA[1] + moonA[2] * moonA[2]);

  const bool MOON_VACUUM = moon_.atmosphere()->isVacuum();

  // Moon terrain at the mean-radius surface point.
  const double MOON_SURF[3] = {cel::moon::R_MEAN, 0.0, 0.0};
  double moonH = 0.0;
  (void)moon_.terrain()->elevationAtEcef(MOON_SURF, moonH);

  log->info(label(), "smoke-check: env queries on both bodies");
  log->info(label(), fmt::format("  earth: |g|={:.3f} m/s^2 @ 7000km   rho_sl={:.4f} kg/m^3   "
                                 "H@equator={:.3f} m",
                                 EARTH_G_MAG, earthRho, earthH));
  log->info(label(), fmt::format("  moon : |g|={:.3f} m/s^2 @ 2000km   vacuum={}             "
                                 "H@equator={:.3f} m",
                                 MOON_G_MAG, MOON_VACUUM, moonH));
}

} // namespace rover_terrain
} // namespace appsim
