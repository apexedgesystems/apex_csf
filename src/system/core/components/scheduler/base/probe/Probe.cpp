/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/system/core/components/scheduler/base/inc/IScheduler.hpp"

namespace {

struct ProbeScheduler final : system_core::scheduler::IScheduler {
  [[nodiscard]] uint16_t fundamentalFreq() const noexcept override { return 100; }
  void tick() noexcept override { ++ticks_; }
  [[nodiscard]] uint64_t tickCount() const noexcept override { return ticks_; }
  [[nodiscard]] size_t taskCount() const noexcept override { return 0; }

  uint64_t ticks_{0};
};

} // namespace

uint64_t probe() {
  ProbeScheduler sched;
  system_core::scheduler::IScheduler* iface = &sched;
  iface->tick();

  return iface->tickCount() + iface->taskCount() + iface->fundamentalFreq();
}
