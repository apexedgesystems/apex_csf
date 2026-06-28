/**
 * @file AircraftControllers_pTest.cpp
 * @brief Throughput benchmarks for the aircraft control loops.
 *
 * One representative loop from each group (longitudinal, lateral, feedforward).
 */

#include <cstdio>

#include "src/bench/inc/Perf.hpp"
#include "src/sim/gnc/aircraft/inc/GustAlleviation.hpp"
#include "src/sim/gnc/aircraft/inc/LateralControllers.hpp"
#include "src/sim/gnc/aircraft/inc/LongitudinalControllers.hpp"

PERF_TEST(PitchAttitudeHoldStep, Throughput) {
  UB_PERF_GUARD(perf);
  sim::gnc::aircraft::PitchAttitudeHold ctl;
  volatile double sink = 0.0;
  auto result =
      perf.throughputLoop([&] { sink += ctl.step(0.10, 0.05, 0.04); }, "pitch_attitude_hold_step");
  std::printf("\n[PitchAttitudeHold] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

PERF_TEST(YawDamperStep, Throughput) {
  UB_PERF_GUARD(perf);
  sim::gnc::aircraft::YawDamper ctl;
  volatile double sink = 0.0;
  auto result = perf.throughputLoop([&] { sink += ctl.step(0.05, 0.04); }, "yaw_damper_step");
  std::printf("\n[YawDamper] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

PERF_TEST(GustAlleviationStep, Throughput) {
  UB_PERF_GUARD(perf);
  sim::gnc::aircraft::GustAlleviation ga;
  volatile double sink = 0.0;
  auto result = perf.throughputLoop([&] { sink += ga.step(2.0, 235.0); }, "gust_alleviation_step");
  std::printf("\n[GustAlleviation] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

PERF_MAIN()
