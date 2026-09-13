/**
 * @file RoverMcuExecutive.cpp
 * @brief Component registration + post-init smoke check for rover_mcu.
 */

#include "demos/apex_horizon_demo/rover_mcu/exec/inc/RoverMcuExecutive.hpp"

#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"

#include <fmt/format.h>

namespace appsim {
namespace rover_mcu {

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

bool RoverMcuExecutive::registerComponents() noexcept {
  const auto& LOG_DIR = fileSystem().logDir();
  auto* log = sysLog();

  if (!registerComponent(&earth_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(earth) FAILED");
    return false;
  }
  // An unbound world parks the rover behind a healthy-looking
  // executive; refuse the boot at error severity instead.
  if (!earth_.isReady()) {
    if (log != nullptr) {
      log->error(label(), static_cast<std::uint8_t>(1),
                 "earth is not ready after registration (world binding refused) -- refusing boot");
    }
    return false;
  }

  rover_.setBody(&earth_);
  rover_.setDriveCommand(controller_.driveCommand());
  if (!registerComponent(&rover_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(rover) FAILED");
    return false;
  }

  controller_.setVehicle(&rover_);
  if (!registerComponent(&controller_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(controller) FAILED");
    return false;
  }

  bridge_.setResolver(bridgeResolverFn, static_cast<void*>(&registry()));
  if (!registerComponent(&bridge_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(bridge) FAILED");
    return false;
  }

  if (log != nullptr) {
    log->info(label(), fmt::format("registered: earth_uid={:#x} rover_uid={:#x} "
                                   "controller_uid={:#x} bridge_uid={:#x}",
                                   earth_.fullUid(), rover_.fullUid(), controller_.fullUid(),
                                   bridge_.fullUid()));
  }
  return true;
}

/* ----------------------------- configureComponents ----------------------------- */

void RoverMcuExecutive::configureComponents() noexcept {
  // One sequence at a time: every catalog RTS shares an exclusion
  // group, so a halt sequence cancels the tour it interrupts and a
  // tour started over another replaces it; the policy survives the
  // catalog rescan an upload triggers.
  actionComponent().setRtsExclusionGroup(1u);

  auto* log = sysLog();
  if (log == nullptr) {
    return;
  }
  const auto& p = rover_.tunables_const();
  const auto& c = controller_.tunables().get();
  log->info(label(),
            fmt::format("smoke-check: rover boots {} at grid ({:+.1f} N, {:+.1f} E) m about "
                        "({:.4f}, {:.4f}), heading {:.1f} deg, step {} Hz; controller boot_mode={} "
                        "cruise={:.2f} tol={:.2f} m",
                        p.init_from_grid != 0u ? "on the grid" : "geodetic", p.init_north_m,
                        p.init_east_m, p.anchor_lat_deg, p.anchor_lon_deg, p.init_heading_deg,
                        p.step_hz, c.boot_mode, c.cruise_throttle_frac, c.arrival_tolerance_m));
}

} // namespace rover_mcu
} // namespace appsim
