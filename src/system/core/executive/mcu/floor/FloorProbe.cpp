/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
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
