/**
 * @file Gnc_pTest.cpp
 * @brief Throughput benchmarks for the control laws.
 *
 * Measures the per-step cost of the PID primitive and a representative loop
 * from each group (longitudinal, lateral, feedforward).
 */

#include <cstdio>

#include "src/bench/inc/Perf.hpp"
#include "src/sim/gnc/inc/GustAlleviation.hpp"
#include "src/sim/gnc/inc/LateralControllers.hpp"
#include "src/sim/gnc/inc/LongitudinalControllers.hpp"
#include "src/sim/gnc/inc/PIDLoop.hpp"

PERF_TEST(PIDLoopStep, Throughput) {
  UB_PERF_GUARD(perf);
  sim::gnc::PIDLoop pid({1.5, 0.1, 0.4});
  volatile double sink = 0.0;
  double e = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        e += 0.001;
        sink += pid.step(e, 0.04);
      },
      "pid_step");
  std::printf("\n[PIDLoop] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

PERF_TEST(PitchAttitudeHoldStep, Throughput) {
  UB_PERF_GUARD(perf);
  sim::gnc::PitchAttitudeHold ctl;
  volatile double sink = 0.0;
  auto result =
      perf.throughputLoop([&] { sink += ctl.step(0.10, 0.05, 0.04); }, "pitch_attitude_hold_step");
  std::printf("\n[PitchAttitudeHold] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

PERF_TEST(YawDamperStep, Throughput) {
  UB_PERF_GUARD(perf);
  sim::gnc::YawDamper ctl;
  volatile double sink = 0.0;
  auto result = perf.throughputLoop([&] { sink += ctl.step(0.05, 0.04); }, "yaw_damper_step");
  std::printf("\n[YawDamper] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

PERF_TEST(GustAlleviationStep, Throughput) {
  UB_PERF_GUARD(perf);
  sim::gnc::GustAlleviation ga;
  volatile double sink = 0.0;
  auto result = perf.throughputLoop([&] { sink += ga.step(2.0, 235.0); }, "gust_alleviation_step");
  std::printf("\n[GustAlleviation] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

PERF_MAIN()
