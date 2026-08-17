/**
 * @file SpecExecutive.cpp
 * @brief Spec demo component registration.
 */

#include "demos/apex_spec_demo/exec/inc/SpecExecutive.hpp"

#include <fmt/format.h>

#include <iterator>

namespace appsim {
namespace exec {

bool SpecExecutive::registerComponents() noexcept {
  const auto& LOG_DIR = fileSystem().logDir();

  // Registration order fixes instanceIndex assignment: the two
  // SpecChannel members become instances 0 and 1 (0xDA00 / 0xDA01).
  system_core::system_component::SystemComponentBase* components[] = {
      &sensor_,   &actuator_, &busDriver_, &matrix_,     &limits_,
      &protoMax_, &channelA_, &channelB_,  &sysMonitor_,
  };
  for (auto* comp : components) {
    if (!registerComponent(comp, LOG_DIR)) {
      return false;
    }
  }

  if (auto* log = sysLog()) {
    log->info("SPEC_DEMO_EXEC",
              fmt::format("Registered {} spec components (sensor={:#x} .. chB={:#x})",
                          static_cast<int>(std::size(components)) - 1, sensor_.fullUid(),
                          channelB_.fullUid()));
  }
  return true;
}

} // namespace exec
} // namespace appsim
