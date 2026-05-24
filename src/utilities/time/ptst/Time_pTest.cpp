/**
 * @file Time_pTest.cpp
 * @brief Performance tests for time utilities.
 *
 * Measures:
 *  - Clock read latency: MONOTONIC, UTC, TAI, GPS via SystemClocks
 *  - Time conversion throughput: UTC-to-TAI, TAI-to-GPS, unit conversions
 *
 * Usage:
 *   ./Time_PTEST --csv results.csv
 *   ./Time_PTEST --quick
 *   ./Time_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/time/inc/SystemClocks.hpp"
#include "src/utilities/time/inc/TimeConvert.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>

namespace ub = vernier::bench;

/* ----------------------------- Clock Read Latency ----------------------------- */

/**
 * @brief Compare clock read latency across time standards.
 *
 * All clocks use clock_gettime under the hood (vDSO-accelerated).
 */
PERF_TEST(Clock, ReadLatency) {
  UB_PERF_GUARD(perf);

  std::printf("\n=== Clock Read Latency ===\n");

  // MONOTONIC
  {
    volatile uint64_t sink = 0;
    auto result = perf.throughputLoop([&] { sink = apex::time::monotonicMicroseconds(nullptr); },
                                      "monotonic");

    std::printf("MONOTONIC: %.0f reads/s (%.1f ns/read)\n", result.callsPerSecond,
                result.stats.median * 1000.0);
  }

  // UTC
  {
    volatile uint64_t sink = 0;
    auto result = perf.throughputLoop([&] { sink = apex::time::utcMicroseconds(nullptr); }, "utc");

    std::printf("UTC:       %.0f reads/s (%.1f ns/read)\n", result.callsPerSecond,
                result.stats.median * 1000.0);
  }
}

/* ----------------------------- Time Conversions ----------------------------- */

/**
 * @brief Time standard conversion throughput.
 *
 * All conversions are O(1) integer arithmetic.
 */
PERF_TEST(Convert, StandardConversions) {
  UB_PERF_GUARD(perf);

  constexpr uint64_t UTC_SAMPLE = 1711324800000000ULL; // 2024-03-25 00:00:00 UTC
  constexpr int32_t LEAP_SECONDS = 37;

  std::printf("\n=== Time Standard Conversions ===\n");

  // UTC -> TAI
  {
    volatile uint64_t sink = 0;
    auto result = perf.throughputLoop(
        [&] { sink = apex::time::utcToTai(UTC_SAMPLE, LEAP_SECONDS); }, "utc-to-tai");

    std::printf("UTC->TAI: %.0f ops/s (%.1f ns/op)\n", result.callsPerSecond,
                result.stats.median * 1000.0);
  }

  // TAI -> GPS
  {
    uint64_t taiSample = apex::time::utcToTai(UTC_SAMPLE, LEAP_SECONDS);
    volatile uint64_t sink = 0;
    auto result =
        perf.throughputLoop([&] { sink = apex::time::taiToGps(taiSample); }, "tai-to-gps");

    std::printf("TAI->GPS: %.0f ops/s (%.1f ns/op)\n", result.callsPerSecond,
                result.stats.median * 1000.0);
  }
}

/**
 * @brief Unit conversion throughput.
 */
PERF_TEST(Convert, UnitConversions) {
  UB_PERF_GUARD(perf);

  volatile uint64_t sink = 0;
  auto result =
      perf.throughputLoop([&] { sink = apex::time::secondsToMicroseconds(3.14159); }, "sec-to-us");

  std::printf("\nsecondsToMicroseconds: %.0f ops/s (%.1f ns/op)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

PERF_MAIN()
