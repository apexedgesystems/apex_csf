/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
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
