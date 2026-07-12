/**
 * @file Frames_pTest.cpp
 * @brief Throughput baselines for the SE(3) transform operations (the
 *        per-hop costs a graph resolve pays).
 */

#include <cstdio>

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"

namespace fr = apex::math::frames;

PERF_TEST(FramesPerf, TransformPoint) {
  UB_PERF_GUARD(perf);
  fr::Transform<double> x;
  x.rotation().setFromEuler321(0.2, -0.3, 0.9);
  x.t[0] = 1.0;
  const double P[3] = {0.5, -1.5, 2.5};
  double out[3];
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        (void)fr::transformPointInto(x, P, out);
        sink = sink + out[0];
      },
      "frames_transform_point");
  std::printf("\n[frames] transformPoint: %.0f ops/s (%.1f ns)\n", result.callsPerSecond,
              1.0e9 / result.callsPerSecond);
}

PERF_TEST(FramesPerf, Compose) {
  UB_PERF_GUARD(perf);
  fr::Transform<double> a, b, out;
  a.rotation().setFromEuler321(0.2, -0.3, 0.9);
  a.t[0] = 1.0;
  b.rotation().setFromAngleAxis(0.7, 0.0, 1.0, 0.0);
  b.t[1] = 4.0;
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        (void)fr::composeInto(a, b, out);
        sink = sink + out.t[0];
      },
      "frames_compose");
  std::printf("\n[frames] compose: %.0f ops/s (%.1f ns)\n", result.callsPerSecond,
              1.0e9 / result.callsPerSecond);
}
PERF_MAIN()
