/**
 * @file ActionDemoExecutive.cpp
 * @brief Action demo component registration implementation.
 */

#include "apps/apex_action_demo/exec/inc/ActionDemoExecutive.hpp"

#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"
#include "src/utilities/time/inc/SystemClocks.hpp"

#include <fmt/format.h>

namespace appsim {
namespace exec {

/* ----------------------------- Resolver Delegate ----------------------------- */

/**
 * @brief Resolver for DataTransform target resolution.
 *
 * Maps (fullUid, category) to a mutable byte pointer via the registry.
 * Same pattern as the executive's internal action engine resolver.
 */
static system_core::support::ResolvedData
transformResolverFn(void* ctx, std::uint32_t fullUid,
                    system_core::data::DataCategory category) noexcept {
  auto* registry = static_cast<system_core::registry::ApexRegistry*>(ctx);
  auto* entry = registry->getData(fullUid, category);
  if (entry == nullptr || !entry->isValid()) {
    return {};
  }
  return {const_cast<std::uint8_t*>(reinterpret_cast<const std::uint8_t*>(entry->dataPtr)),
          entry->size};
}

/* ----------------------------- ActionDemoExecutive Methods ----------------------------- */

bool ActionDemoExecutive::registerComponents() noexcept {
  const auto& LOG_DIR = fileSystem().logDir();
  auto* log = sysLog();

  // SensorModel
  if (!registerComponent(&sensor_, LOG_DIR)) {
    return false;
  }

  // DataTransform - wire resolver and clock frequency for ATS time conversion
  dataTransform_.setResolver(transformResolverFn, static_cast<void*>(&registry()));
  dataTransform_.setClockFrequency(10); // 10 Hz clock — converts cycles to microseconds for ATS
  if (!registerComponent(&dataTransform_, LOG_DIR)) {
    return false;
  }

  // SystemMonitor
  if (!registerComponent(&sysMonitor_, LOG_DIR)) {
    return false;
  }

  if (log != nullptr) {
    log->info("ACTION_DEMO_EXEC",
              fmt::format("Registered: sensor={:#x} transform={:#x} sysmon={:#x}",
                          sensor_.fullUid(), dataTransform_.fullUid(), sysMonitor_.fullUid()));
  }

  return true;
}

void ActionDemoExecutive::configureComponents() noexcept {
  // Wire monotonic time provider for ATS sequences.
  // ATS delayCycles are interpreted as microseconds when a time provider is set.
  actionComponent().iface().timeProvider = {apex::time::monotonicMicroseconds, nullptr};

  auto* log = sysLog();
  if (log != nullptr) {
    log->info("ACTION_DEMO_EXEC", "Time provider wired: MONOTONIC (microseconds)");
  }
}

} // namespace exec
} // namespace appsim
