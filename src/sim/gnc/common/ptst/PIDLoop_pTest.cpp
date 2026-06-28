/**
 * @file PIDLoop_pTest.cpp
 * @brief Throughput benchmark for the PID primitive.
 */

#include <cstdio>

#include "src/bench/inc/Perf.hpp"
#include "src/sim/gnc/common/inc/PIDLoop.hpp"

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

PERF_MAIN()
