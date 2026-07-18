/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
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
