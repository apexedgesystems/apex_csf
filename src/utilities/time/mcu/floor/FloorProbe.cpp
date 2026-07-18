/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
 */

#include "src/utilities/time/mcu/inc/TimeBase.hpp"
#include "src/utilities/time/mcu/inc/TimeConvert.hpp"

using namespace apex::time;

static uint64_t tickMicros(void*) noexcept { return 42; }

uint64_t probe() {
  TimeProviderDelegate provider{&tickMicros, nullptr};

  Timestamp a{provider(), TimeStandard::MONOTONIC};
  Timestamp b{utcToTai(a.microseconds), TimeStandard::MONOTONIC};

  const uint64_t GPS = utcToGps(secondsToMicroseconds(1.5));
  const uint32_t CYCLES = secondsToCycles(2.0, 100);

  return a.microseconds + b.microseconds + GPS + CYCLES + static_cast<uint64_t>(a < b) +
         static_cast<uint64_t>(a != b) + cyclesToMicroseconds(CYCLES, 100) +
         static_cast<uint64_t>(*toString(a.standard));
}
