/**
 * @file LidarBoxExecutive.cpp
 * @brief Implementation of the lidar_box demo executive.
 */

#include "demos/apex_horizon_demo/lidar_box/exec/inc/LidarBoxExecutive.hpp"

#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"
#include "src/system/core/support/shm_ring_bridge/inc/ShmRingBridgeData.hpp"

#include <cstdint>
#include <fmt/format.h>

namespace appsim {
namespace exec {

/* ----------------------------- Bridge resolver ----------------------------- */

// Maps (fullUid, category) to the registered byte block via the registry,
// returning a read-only pointer for the bridge to memcpy each tick.
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

bool LidarBoxExecutive::registerComponents() noexcept {
  const auto& LOG_DIR = fileSystem().logDir();
  auto* log = sysLog();

  // Tunables come from per-component .tprm files, loaded by the framework
  // between registration and init. The C++ side only wires cross-component
  // plumbing (the bridge's registry resolver).
  if (!registerComponent(&producer_, LOG_DIR)) {
    if (log != nullptr) {
      log->info(label(), "registerComponent(producer) FAILED");
    }
    return false;
  }

  // The bridge's TPRM selects WHICH (uid, category) block to publish; the
  // resolver turns that selection into a live byte pointer. Failure to open
  // the channel is non-fatal -- the sim runs, nothing publishes.
  bridge_.setResolver(bridgeResolverFn, static_cast<void*>(&registry()));
  if (!registerComponent(&bridge_, LOG_DIR)) {
    if (log != nullptr) {
      log->info(label(), "registerComponent(bridge) FAILED");
    }
    return false;
  }

  if (log != nullptr) {
    log->info(label(), fmt::format("registered: producer_uid={:#x} bridge_uid={:#x}",
                                   producer_.fullUid(), bridge_.fullUid()));
  }
  return true;
}

/* ----------------------------- configureComponents ----------------------------- */

void LidarBoxExecutive::configureComponents() noexcept {
  auto* log = sysLog();
  if (log == nullptr) {
    return;
  }

  const auto& S = producer_.tunables().get(); // the tunable-owned scene
  const auto& T = bridge_.tunables().get();
  log->info(label(), fmt::format("lidar_box up: box=({:.1f},{:.1f},{:.1f}) mount={:.2f} | "
                                 "bridge shm={} app_magic={:#x} payload={}B cap={} src_uid={:#x}",
                                 S.box_half_x_m, S.box_half_y_m, S.box_half_z_m, S.mount_radius_m,
                                 T.shm_path[0] != '\0' ? T.shm_path : "(unset)", T.app_magic,
                                 T.payload_size, T.capacity, T.source_uid));
}

} // namespace exec
} // namespace appsim
