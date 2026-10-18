/**
 * @file LegendreScalar_pTest.cpp
 * @brief Throughput of the scalar Legendre functions beside the standard library's.
 *
 * Each (n, m) case times the library's normalized column walk and, where the
 * standard library provides the C++17 special math functions, std::sph_legendre
 * at the same point, so the comparison is measured on whatever host runs it.
 * The degrees span a low order, the middle of a column, the diagonal and the
 * EGM2008 degree.
 *
 * Usage:
 *   ./LegendreScalar_PTEST --cycles 200000 --csv results.csv
 *
 * Sub-microsecond calls: pass an explicit --cycles rather than relying on a
 * time target.
 */

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/math/legendre/inc/AssociatedLegendre.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>

using apex::math::legendre::associatedLegendre;
using apex::math::legendre::normalizedAssociatedLegendre;

namespace {

constexpr double X = 0.3; ///< Interior argument; every case below is finite and normal here.

class LegendreScalarPerf : public ::testing::TestWithParam<std::tuple<int, int>> {};

std::string caseLabel(const char* what, int n, int m) {
  return std::string(what) + "_n" + std::to_string(n) + "_m" + std::to_string(m);
}

} // namespace

/* ----------------------------- Library ----------------------------- */

/** @brief Normalized Pbar_n^m(x): the column walk from (m, m) to (n, m). */
TEST_P(LegendreScalarPerf, Normalized) {
  UB_PERF_GUARD(perf);
  const auto [N, M] = GetParam();
  volatile double sink = 0.0;
  auto result = perf.throughputLoop([&] { sink = normalizedAssociatedLegendre(N, M, X); },
                                    caseLabel("normalized", N, M));
  std::printf("\n[legendre] normalized (%d, %d): %.1f ns\n", N, M, 1.0e9 / result.callsPerSecond);
}

/** @brief Unnormalized P_n^m(x) where it is finite at x = 0.3 (n below 128 here). */
TEST_P(LegendreScalarPerf, Unnormalized) {
  UB_PERF_GUARD(perf);
  const auto [N, M] = GetParam();
  if (!std::isfinite(associatedLegendre(N, M, X))) {
    GTEST_SKIP() << "P_" << N << "^" << M << "(" << X << ") exceeds DBL_MAX";
  }
  volatile double sink = 0.0;
  auto result = perf.throughputLoop([&] { sink = associatedLegendre(N, M, X); },
                                    caseLabel("unnormalized", N, M));
  std::printf("\n[legendre] unnormalized (%d, %d): %.1f ns\n", N, M, 1.0e9 / result.callsPerSecond);
}

/* ----------------------------- Standard Library ----------------------------- */

/** @brief std::sph_legendre at the same point, the reference for the normalized walk. */
TEST_P(LegendreScalarPerf, StdSphLegendre) {
#if defined(__cpp_lib_math_special_functions)
  UB_PERF_GUARD(perf);
  const auto [N, M] = GetParam();
  const double THETA = std::acos(X);
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] { sink = std::sph_legendre(static_cast<unsigned>(N), static_cast<unsigned>(M), THETA); },
      caseLabel("std_sph_legendre", N, M));
  std::printf("\n[legendre] std::sph_legendre (%d, %d): %.1f ns\n", N, M,
              1.0e9 / result.callsPerSecond);
#else
  GTEST_SKIP() << "C++17 special math functions unavailable in this standard library";
#endif
}

INSTANTIATE_TEST_SUITE_P(Degrees, LegendreScalarPerf,
                         ::testing::Values(std::make_tuple(10, 3), std::make_tuple(50, 25),
                                           std::make_tuple(180, 90), std::make_tuple(180, 180),
                                           std::make_tuple(720, 360), std::make_tuple(2190, 1000)));

PERF_MAIN()
