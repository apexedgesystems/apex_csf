/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/system/core/executive/core/inc/ExecutiveCore.hpp"

namespace {

struct ProbeExecutive final : executive::ExecutiveCore {
  [[nodiscard]] executive::RunResult run() noexcept override {
    return executive::RunResult::SUCCESS;
  }
  void shutdown() noexcept override { stop_ = true; }
  [[nodiscard]] bool isShutdownRequested() const noexcept override { return stop_; }
  [[nodiscard]] uint64_t cycleCount() const noexcept override { return 0; }

  bool stop_{false};
};

} // namespace

uint64_t probe() {
  ProbeExecutive exec;
  exec.shutdown();

  static_assert(executive::ExecutiveCore::COMPONENT_ID == 0, "executive is the root component");
  return static_cast<uint64_t>(exec.run()) + exec.cycleCount() +
         static_cast<uint64_t>(exec.isShutdownRequested());
}
