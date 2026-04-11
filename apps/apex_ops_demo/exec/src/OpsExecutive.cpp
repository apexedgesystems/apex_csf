/**
 * @file OpsExecutive.cpp
 * @brief Ops demo component registration implementation.
 */

#include "apps/apex_ops_demo/exec/inc/OpsExecutive.hpp"

#include <fmt/format.h>

namespace appsim {
namespace exec {

/* ----------------------------- OpsExecutive Methods ----------------------------- */

bool OpsExecutive::registerComponents() noexcept {
  const auto& LOG_DIR = fileSystem().logDir();
  auto* log = sysLog();

  // WaveGenerator instance 0 (fullUid = 0x00D000)
  if (!registerComponent(&waveGen0_, LOG_DIR)) {
    return false;
  }

  // WaveGenerator instance 1 (fullUid = 0x00D001, auto-assigned instanceIndex)
  if (!registerComponent(&waveGen1_, LOG_DIR)) {
    return false;
  }

  // TelemetryManager (fullUid = 0x00C900)
  // Wire registry pointer so it can read data blocks for push telemetry.
  telemetryMgr_.setRegistry(&registry());
  if (!registerComponent(&telemetryMgr_, LOG_DIR)) {
    return false;
  }

  // SystemMonitor (fullUid = 0x00C800)
  if (!registerComponent(&sysMonitor_, LOG_DIR)) {
    return false;
  }

  // TestPlugin for hot-swap testing (fullUid = 0x00FA00)
  if (!registerComponent(&testPlugin_, LOG_DIR)) {
    return false;
  }

  if (log != nullptr) {
    log->info("OPS_DEMO_EXEC",
              fmt::format("Registered: wave0={:#x} wave1={:#x} tlmMgr={:#x} "
                          "sysmon={:#x} plugin={:#x}",
                          waveGen0_.fullUid(), waveGen1_.fullUid(), telemetryMgr_.fullUid(),
                          sysMonitor_.fullUid(), testPlugin_.fullUid()));
  }

  return true;
}

} // namespace exec
} // namespace appsim
