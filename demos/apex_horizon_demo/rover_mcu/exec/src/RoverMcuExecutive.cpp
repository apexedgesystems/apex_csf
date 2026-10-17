/**
 * @file RoverMcuExecutive.cpp
 * @brief Component registration + post-init smoke check for rover_mcu.
 */

#include "demos/apex_horizon_demo/rover_mcu/exec/inc/RoverMcuExecutive.hpp"

#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <vector>

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
  controller_.setBoardLink(&link_.snapshot());
  if (!registerComponent(&controller_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(controller) FAILED");
    return false;
  }

  link_.setSources(&rover_, &controller_);
  if (!registerComponent(&link_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(board link) FAILED");
    return false;
  }

  lidar_.setSource(&rover_.telemetry());
  if (!registerComponent(&lidar_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(lidar model) FAILED");
    return false;
  }

  bridge_.setResolver(bridgeResolverFn, static_cast<void*>(&registry()));
  if (!registerComponent(&bridge_, LOG_DIR)) {
    if (log != nullptr)
      log->info(label(), "registerComponent(bridge) FAILED");
    return false;
  }
  // The pushed-telemetry dispatcher reads the registered OUTPUT blocks
  // straight from the registry, so it is wired to it before it registers.
  telemetry_.setRegistry(&registry());
  if (!registerComponent(&telemetry_, LOG_DIR)) {
    if (log != nullptr) {
      log->info(label(), "registerComponent(telemetry) FAILED");
    }
    return false;
  }

  if (log != nullptr) {
    log->info(label(), fmt::format("registered: earth_uid={:#x} rover_uid={:#x} "
                                   "controller_uid={:#x} link_uid={:#x} lidar_uid={:#x} "
                                   "bridge_uid={:#x} telemetry_uid={:#x}",
                                   earth_.fullUid(), rover_.fullUid(), controller_.fullUid(),
                                   link_.fullUid(), lidar_.fullUid(), bridge_.fullUid(),
                                   telemetry_.fullUid()));
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
  startBootSequence();

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

/* ----------------------------- startBootSequence ----------------------------- */

void RoverMcuExecutive::startBootSequence() noexcept {
  // The demo comes up running. The boot behaviour itself is data: the
  // framework has already loaded the ats/ bank into its slots, so what
  // is left is to start the armed one. Which sequence it then runs, and
  // whether it runs at all, is in that file and in the manifest row that
  // packs it -- no sequence is named here.
  //
  // Starting it now, before the scheduler runs, puts its first step on
  // the first action phase -- which the executive runs after that
  // cycle's tasks, so the plant has latched its configured pose by then:
  // a first leg expressed relative to the rover measures from where the
  // rover actually is.
  auto& action = actionComponent();
  auto* log = sysLog();
  constexpr std::uint8_t ATS_FIRST_SLOT =
      static_cast<std::uint8_t>(system_core::data::Config::RTS_SLOT_COUNT);

  for (std::uint8_t slot = ATS_FIRST_SLOT;
       slot < static_cast<std::uint8_t>(system_core::data::SEQUENCE_TABLE_SIZE); ++slot) {
    const auto& SEQ = action.iface().sequences[slot];
    if (SEQ.type != system_core::data::SequenceType::ATS || !SEQ.armed || SEQ.stepCount == 0u) {
      continue;
    }
    const std::uint8_t PAYLOAD[1] = {slot};
    std::vector<std::uint8_t> response;
    const std::uint8_t RC =
        action.handleCommand(0x0504u, apex::compat::rospan<std::uint8_t>(PAYLOAD, 1), response);
    if (log != nullptr) {
      if (RC == 0u) {
        log->info(label(), fmt::format("boot sequence: ATS id={} slot={} started ({} steps)",
                                       SEQ.sequenceId, slot, SEQ.stepCount));
      } else {
        log->warning(label(), RC,
                     fmt::format("boot sequence: ATS id={} slot={} refused (rc={})", SEQ.sequenceId,
                                 slot, RC));
      }
    }
    return; // One boot sequence: the first armed ATS in the bank.
  }

  if (log != nullptr) {
    log->info(label(), "boot sequence: none packed, the rover boots idle");
  }
}

} // namespace rover_mcu
} // namespace appsim
