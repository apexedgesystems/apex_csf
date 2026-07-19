/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/system/core/executive/base/inc/IExecutive.hpp"

namespace {

struct ProbeExecutive final : executive::IExecutive {
  [[nodiscard]] executive::RunResult run() noexcept override {
    return executive::RunResult::SUCCESS;
  }
  void shutdown() noexcept override { stop_ = true; }
  [[nodiscard]] bool isShutdownRequested() const noexcept override { return stop_; }
  [[nodiscard]] uint64_t cycleCount() const noexcept override { return 1; }

  bool stop_{false};
};

} // namespace

uint64_t probe() {
  ProbeExecutive exec;
  executive::IExecutive* iface = &exec;
  iface->shutdown();

  return static_cast<uint64_t>(iface->run()) + iface->cycleCount() +
         static_cast<uint64_t>(iface->isShutdownRequested()) +
         static_cast<uint64_t>(executive::RunResult::ERROR_RUNTIME);
}
