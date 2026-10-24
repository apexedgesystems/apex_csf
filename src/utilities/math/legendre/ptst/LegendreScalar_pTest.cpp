/**
 * @file LegendreScalar_pTest.cpp
 * @brief Throughput of the scalar Legendre functions beside the standard library's.
 *
 * Each (n, m) case times the library's normalized column walk, stateless and
 * from precomputed tables, and, where the
 * standard library provides the C++17 special math functions, std::sph_legendre
 * at the same point, so the comparison is measured on whatever host runs it.
 * The degrees span a low order, the middle of a column, the diagonal and the
 * EGM2008 degree.
 *
 * Usage:
 *   ./LegendreScalar_PTEST --cycles 200000 --csv results.csv
 *
 * Every case warms its call before the timed loop, so a cold first call is
 * not in the measurement.
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
using apex::math::legendre::makeScalarTables;
using apex::math::legendre::normalizedAssociatedLegendre;
using apex::math::legendre::ScalarTables;

namespace {

constexpr double X = 0.3; ///< Interior argument; every case below is finite and normal here.

class LegendreScalarPerf : public ::testing::TestWithParam<std::tuple<int, int>> {};

/// Tables for every case, built once outside the measured loops.
const ScalarTables& benchTables() {
  static const ScalarTables TABLES = makeScalarTables(2190);
  return TABLES;
}

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
  perf.warmup([&] { sink = normalizedAssociatedLegendre(N, M, X); });
  auto result = perf.throughputLoop([&] { sink = normalizedAssociatedLegendre(N, M, X); },
                                    caseLabel("normalized", N, M));
  std::printf("\n[legendre] normalized (%d, %d): %.1f ns\n", N, M, 1.0e9 / result.callsPerSecond);
}

/** @brief Normalized Pbar_n^m(x) from precomputed tables. */
TEST_P(LegendreScalarPerf, NormalizedCached) {
  UB_PERF_GUARD(perf);
  const auto [N, M] = GetParam();
  const ScalarTables& TABLES = benchTables();
  volatile double sink = 0.0;
  perf.warmup([&] { sink = normalizedAssociatedLegendre(N, M, X, TABLES); });
  auto result = perf.throughputLoop([&] { sink = normalizedAssociatedLegendre(N, M, X, TABLES); },
                                    caseLabel("normalized_cached", N, M));
  std::printf("\n[legendre] normalized cached (%d, %d): %.1f ns\n", N, M,
              1.0e9 / result.callsPerSecond);
}

/** @brief Unnormalized P_n^m(x) where it is finite at x = 0.3 (n below 128 here). */
TEST_P(LegendreScalarPerf, Unnormalized) {
  UB_PERF_GUARD(perf);
  const auto [N, M] = GetParam();
  if (!std::isfinite(associatedLegendre(N, M, X))) {
    GTEST_SKIP() << "P_" << N << "^" << M << "(" << X << ") exceeds DBL_MAX";
  }
  volatile double sink = 0.0;
  perf.warmup([&] { sink = associatedLegendre(N, M, X); });
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
  perf.warmup(
      [&] { sink = std::sph_legendre(static_cast<unsigned>(N), static_cast<unsigned>(M), THETA); });
  auto result = perf.throughputLoop(
      [&] { sink = std::sph_legendre(static_cast<unsigned>(N), static_cast<unsigned>(M), THETA); },
      caseLabel("std_sph_legendre", N, M));
  std::printf("\n[legendre] std::sph_legendre (%d, %d): %.1f ns\n", N, M,
              1.0e9 / result.callsPerSecond);
#else
  GTEST_SKIP() << "C++17 special math functions unavailable in this standard library";
#endif
}

/* ----------------------------- Range Regimes ----------------------------- */

/// One (n, m, x) whose evaluation exercises a particular range path of the
/// unnormalized walk; a row is skipped where the value is not finite.
struct RangeCase {
  const char* label;
  int n, m;
  double x;
};

class LegendreScalarRangePerf : public ::testing::TestWithParam<RangeCase> {};

/** @brief Unnormalized P_n^m(x) across the walk's range regimes. */
TEST_P(LegendreScalarRangePerf, Unnormalized) {
  UB_PERF_GUARD(perf);
  const RangeCase C = GetParam();
  if (!std::isfinite(associatedLegendre(C.n, C.m, C.x))) {
    GTEST_SKIP() << C.label << ": value not finite in this build";
  }
  volatile double sink = 0.0;
  perf.warmup([&] { sink = associatedLegendre(C.n, C.m, C.x); });
  auto result = perf.throughputLoop([&] { sink = associatedLegendre(C.n, C.m, C.x); },
                                    std::string("unnormalized_") + C.label);
  std::printf("\n[legendre] unnormalized %s (%d, %d, %g): %.1f ns\n", C.label, C.n, C.m, C.x,
              1.0e9 / result.callsPerSecond);
}

INSTANTIATE_TEST_SUITE_P(Regimes, LegendreScalarRangePerf,
                         ::testing::Values(RangeCase{"diagonal_seed_scaled", 150, 150, 0.3},
                                           RangeCase{"long_low_order_walk", 2190, 0, 0.3},
                                           RangeCase{"tiny_argument", 6, 0, 0x1p-1074},
                                           RangeCase{"seed_range_recovered", 152, 150, 0.05},
                                           RangeCase{"walk_range_recovered", 1200, 100, 0.9}),
                         [](const ::testing::TestParamInfo<RangeCase>& info) {
                           return std::string(info.param.label);
                         });

INSTANTIATE_TEST_SUITE_P(Degrees, LegendreScalarPerf,
                         ::testing::Values(std::make_tuple(10, 3), std::make_tuple(50, 25),
                                           std::make_tuple(180, 90), std::make_tuple(180, 180),
                                           std::make_tuple(720, 360), std::make_tuple(2190, 1000)));

PERF_MAIN()
