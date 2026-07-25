/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/system/core/components/scheduler/mcu/inc/McuScheduler.hpp"

using namespace system_core::scheduler::mcu;

static void noopTask(void*) noexcept {}

uint64_t probe() {
  McuScheduler<4, uint32_t> sched(100);
  bool added = sched.addTask({{noopTask, nullptr}, 1, 1, 0, 0, 1});
  uint8_t rc = sched.init();
  sched.tick();

  static_assert(McuScheduler<4, uint32_t>::maxTasks() == 4, "static table size is the parameter");
  return sched.tickCount() + sched.taskCount() + static_cast<uint64_t>(added) + rc +
         sched.fundamentalFreq();
}
