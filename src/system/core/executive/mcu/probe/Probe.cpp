/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/system/core/executive/mcu/inc/FreeRunningSource.hpp"
#include "src/system/core/executive/mcu/inc/McuExecutive.hpp"

using namespace executive::mcu;

static void noopTask(void*) noexcept {}

uint64_t probe() {
  FreeRunningSource tickSource(100);
  McuExecutive<4, uint32_t> exec(&tickSource, 100, 1);
  bool added = exec.addTask({{noopTask, nullptr}, 1, 1, 0, 0, 1});

  executive::IExecutive* iface = &exec;
  return exec.cycleCount() + static_cast<uint64_t>(added) +
         static_cast<uint64_t>(iface->isShutdownRequested());
}
