/**
 * @file EdgeExecutive.cpp
 * @brief Edge compute executive component registration.
 */

#include "apps/apex_edge_demo/exec/inc/EdgeExecutive.hpp"

#include <fmt/format.h>

namespace appsim {
namespace exec {

/* ----------------------------- EdgeExecutive Methods ----------------------------- */

bool EdgeExecutive::registerComponents() noexcept {
  const auto& LOG_DIR = fileSystem().logDir();
  auto* log = sysLog();

  // GPU workload models (SW_MODEL)
  if (!registerComponent(&convFilter_, LOG_DIR)) {
    return false;
  }

  if (!registerComponent(&fftAnalyzer_, LOG_DIR)) {
    return false;
  }

  if (!registerComponent(&batchStats_, LOG_DIR)) {
    return false;
  }

  if (!registerComponent(&streamCompact_, LOG_DIR)) {
    return false;
  }

  // System health monitoring (CPU + GPU via NVML)
  if (!registerComponent(&sysMonitor_, LOG_DIR)) {
    return false;
  }

  if (log != nullptr) {
    log->info("EDGE_EXEC",
              fmt::format("Registered: conv={:#x} fft={:#x} batch={:#x} compact={:#x} "
                          "monitor={:#x}",
                          convFilter_.fullUid(), fftAnalyzer_.fullUid(), batchStats_.fullUid(),
                          streamCompact_.fullUid(), sysMonitor_.fullUid()));
  }

  return true;
}

} // namespace exec
} // namespace appsim
