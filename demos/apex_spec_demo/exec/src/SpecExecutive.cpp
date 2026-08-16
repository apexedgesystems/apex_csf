/**
 * @file SpecExecutive.cpp
 * @brief Spec demo component registration.
 */

#include "demos/apex_spec_demo/exec/inc/SpecExecutive.hpp"

#include <fmt/format.h>

namespace appsim {
namespace exec {

bool SpecExecutive::registerComponents() noexcept {
  const auto& LOG_DIR = fileSystem().logDir();

  // SpecSensor (fullUid = 0x00D400) -- TOML-authored spec.
  if (!registerComponent(&sensor_, LOG_DIR)) {
    return false;
  }

  // SpecActuator (fullUid = 0x00D500) -- proto-authored spec.
  if (!registerComponent(&actuator_, LOG_DIR)) {
    return false;
  }

  // SystemMonitor (fullUid = 0x00C800)
  if (!registerComponent(&sysMonitor_, LOG_DIR)) {
    return false;
  }

  if (auto* log = sysLog()) {
    log->info("SPEC_DEMO_EXEC",
              fmt::format("Registered: sensor={:#x} actuator={:#x} sysmon={:#x}", sensor_.fullUid(),
                          actuator_.fullUid(), sysMonitor_.fullUid()));
  }
  return true;
}

} // namespace exec
} // namespace appsim
