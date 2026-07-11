/**
 * @file Vecmat_pTest.cpp
 * @brief Throughput baselines for the fixed-size vector/matrix operations
 *        (the per-tick costs a rigid body pays: cross products, inertia
 *        multiply/solve, DCM construction).
 */

#include <cstdio>

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/math/vecmat/inc/Mat3Ops.hpp"
#include "src/utilities/math/vecmat/inc/Rotations.hpp"
#include "src/utilities/math/vecmat/inc/Vec3Ops.hpp"

namespace vm = apex::math::vecmat;

PERF_TEST(VecmatPerf, CrossAndDot) {
  UB_PERF_GUARD(perf);
  const double A[3] = {1.3, -0.2, 2.1};
  const double B[3] = {-0.7, 0.4, 0.9};
  double c[3];
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        vm::cross(A, B, c);
        sink = sink + vm::dot(A, c);
      },
      "vecmat_cross_dot");
  std::printf("\n[vecmat] cross+dot: %.0f ops/s (%.1f ns)\n", result.callsPerSecond,
              1.0e9 / result.callsPerSecond);
}

PERF_TEST(VecmatPerf, InertiaSolve) {
  UB_PERF_GUARD(perf);
  const double I[9] = {1285, 0, -80, 0, 1825, 0, -80, 0, 2667};
  const double B[3] = {310.2, -180.5, 540.1};
  double x[3];
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        (void)vm::solveInto(I, B, x);
        sink = sink + x[0];
      },
      "vecmat_inertia_solve");
  std::printf("\n[vecmat] inertia solve: %.0f ops/s (%.1f ns)\n", result.callsPerSecond,
              1.0e9 / result.callsPerSecond);
}

PERF_TEST(VecmatPerf, DcmFromEuler) {
  UB_PERF_GUARD(perf);
  double dcm[9];
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        vm::dcmFromEuler321Into(0.3, -0.4, 1.2, dcm);
        sink = sink + dcm[0];
      },
      "vecmat_dcm_euler321");
  std::printf("\n[vecmat] dcmFromEuler321: %.0f ops/s (%.1f ns)\n", result.callsPerSecond,
              1.0e9 / result.callsPerSecond);
}

PERF_MAIN()
