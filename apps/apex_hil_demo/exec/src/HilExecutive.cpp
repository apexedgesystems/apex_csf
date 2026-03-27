/**
 * @file HilExecutive.cpp
 * @brief HIL demonstration executive implementation.
 *
 * Registers all HIL components and wires data flows:
 *   - Transport: Framework auto-provisions (VirtualFlightCtrl PTY),
 *     executive wires peer path to emulated driver.
 *   - Data: Plant -> Drivers (VehicleState), Driver -> Plant (ControlCmd)
 *   - Comparator: Receives pointers to both driver instances.
 *   - Action engine: Configured via TPRM (not hardcoded).
 */

#include "apps/apex_hil_demo/exec/inc/HilExecutive.hpp"

#include <fmt/format.h>

namespace appsim {
namespace exec {

/* ----------------------------- Component Registration ----------------------------- */

bool HilExecutive::registerComponents() noexcept {
  const auto& LOG_DIR = fileSystem().logDir();
  auto* log = sysLog();

  // 1. Plant model (SW_MODEL)
  if (!registerComponent(&plantModel_, LOG_DIR)) {
    return false;
  }

  // 2. Virtual flight controller (HW_MODEL)
  //    Transport is auto-provisioned by the framework during registerComponent().
  if (!registerComponent(&virtualCtrl_, LOG_DIR)) {
    return false;
  }

  // Wire peer device path from HW_MODEL's provisioned transport to emulated driver.
  if (virtualCtrl_.peerDevicePath()[0] != '\0') {
    driverEmulated_.setDevicePath(virtualCtrl_.peerDevicePath());
    if (log != nullptr) {
      log->info("HIL_EXEC", fmt::format("Transport wired: VFC fd={} -> driver slave={}",
                                        virtualCtrl_.transportFd(), virtualCtrl_.peerDevicePath()));
    }
  }

  // 3. Drivers (DRIVER x2)
  //    Both get registered with the same componentId/componentName,
  //    so the executive auto-assigns instanceIndex 0 and 1.
  if (!registerComponent(&driverReal_, LOG_DIR)) {
    return false;
  }
  if (!registerComponent(&driverEmulated_, LOG_DIR)) {
    return false;
  }

  // 4. Comparator (SUPPORT)
  comparator_.setDrivers(&driverReal_, &driverEmulated_);
  if (!registerComponent(&comparator_, LOG_DIR)) {
    return false;
  }

  // 5. System monitor (SUPPORT)
  if (!registerComponent(&sysMonitor_, LOG_DIR)) {
    return false;
  }

  // 6. Test plugin (SW_MODEL) -- hot-swap target for RELOAD_LIBRARY testing
  if (!registerComponent(&testPlugin_, LOG_DIR)) {
    return false;
  }

  // 7. Wire data flows:
  //    Plant -> Drivers: both drivers read plant's published VehicleState.
  //    Driver -> Plant: emulated driver's ControlCmd feeds back to plant thrust.
  //    In SIL mode, only the emulated path is active (real driver has no device).
  driverReal_.setStateSource(&plantModel_.vehicleState());
  driverEmulated_.setStateSource(&plantModel_.vehicleState());
  plantModel_.setControlSource(&driverEmulated_.lastCommand());
  plantModel_.setControlCounter(&driverEmulated_.driverState().rxCount);

  if (log != nullptr) {
    log->info(
        "HIL_EXEC",
        fmt::format("Registered: plant={:#x} vfc={:#x} drv0={:#x} drv1={:#x} comp={:#x} test={:#x}",
                    plantModel_.fullUid(), virtualCtrl_.fullUid(), driverReal_.fullUid(),
                    driverEmulated_.fullUid(), comparator_.fullUid(), testPlugin_.fullUid()));
  }

  return true;
}

} // namespace exec
} // namespace appsim
