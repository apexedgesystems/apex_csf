/**
 * @file Compatibility_pTest.cpp
 * @brief Performance tests for compatibility shim operations.
 *
 * Measures:
 *  - Byteswap throughput across 16/32/64-bit integers
 *  - ByteswapIeee throughput for float and double
 *  - Endian detection runtime overhead (compile-time constant)
 *
 * Usage:
 *   ./Compatibility_PTEST --csv results.csv
 *   ./Compatibility_PTEST --quick
 *   ./Compatibility_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/compatibility/inc/compat_byteswap.hpp"
#include "src/utilities/compatibility/inc/compat_endian.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace ub = vernier::bench;

/* ----------------------------- Test Data ----------------------------- */

static std::vector<uint16_t> generateU16(size_t count, unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::uniform_int_distribution<uint16_t> dist;
  std::vector<uint16_t> data(count);
  for (auto& v : data) {
    v = dist(gen);
  }
  return data;
}

static std::vector<uint32_t> generateU32(size_t count, unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::uniform_int_distribution<uint32_t> dist;
  std::vector<uint32_t> data(count);
  for (auto& v : data) {
    v = dist(gen);
  }
  return data;
}

static std::vector<uint64_t> generateU64(size_t count, unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::uniform_int_distribution<uint64_t> dist;
  std::vector<uint64_t> data(count);
  for (auto& v : data) {
    v = dist(gen);
  }
  return data;
}

static std::vector<double> generateDoubles(size_t count, unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<double> dist(-1e6, 1e6);
  std::vector<double> data(count);
  for (auto& v : data) {
    v = dist(gen);
  }
  return data;
}

/* ----------------------------- Byteswap Integer Comparison ----------------------------- */

/**
 * @brief Compare byteswap throughput across integer widths.
 *
 * Tests with batch of 10000 values to amortize loop overhead.
 */
PERF_TEST(Byteswap, IntegerComparison) {
  UB_PERF_GUARD(perf);

  constexpr size_t BATCH = 10000;
  auto data16 = generateU16(BATCH);
  auto data32 = generateU32(BATCH);
  auto data64 = generateU64(BATCH);

  std::printf("\n=== Byteswap Integer Comparison (batch of %zu) ===\n", BATCH);

  // 16-bit
  {
    volatile uint16_t sink = 0;
    auto result = perf.throughputLoop(
        [&] {
          for (size_t i = 0; i < BATCH; ++i) {
            data16[i] = apex::compat::byteswap(data16[i]);
          }
          sink = data16[0];
        },
        "u16");

    std::printf("uint16: %.0f batches/s (%.1f ns/swap)\n", result.callsPerSecond,
                result.stats.median * 1000.0 / BATCH);
  }

  // 32-bit
  {
    volatile uint32_t sink = 0;
    auto result = perf.throughputLoop(
        [&] {
          for (size_t i = 0; i < BATCH; ++i) {
            data32[i] = apex::compat::byteswap(data32[i]);
          }
          sink = data32[0];
        },
        "u32");

    std::printf("uint32: %.0f batches/s (%.1f ns/swap)\n", result.callsPerSecond,
                result.stats.median * 1000.0 / BATCH);
  }

  // 64-bit
  {
    volatile uint64_t sink = 0;
    auto result = perf.throughputLoop(
        [&] {
          for (size_t i = 0; i < BATCH; ++i) {
            data64[i] = apex::compat::byteswap(data64[i]);
          }
          sink = data64[0];
        },
        "u64");

    std::printf("uint64: %.0f batches/s (%.1f ns/swap)\n", result.callsPerSecond,
                result.stats.median * 1000.0 / BATCH);
  }
}

/* ----------------------------- Byteswap IEEE Throughput ----------------------------- */

/**
 * @brief ByteswapIeee throughput for double-precision values.
 *
 * Tests the IEEE-754 swap path (memcpy + integral swap + memcpy).
 */
PERF_TEST(Byteswap, IeeeThroughput) {
  UB_PERF_GUARD(perf);

  constexpr size_t BATCH = 10000;
  auto data = generateDoubles(BATCH);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (size_t j = 0; j < BATCH; ++j) {
        data[j] = apex::compat::byteswapIeee(data[j]);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (size_t j = 0; j < BATCH; ++j) {
          data[j] = apex::compat::byteswapIeee(data[j]);
        }
        sink = data[0];
      },
      "ieee-double");

  std::printf("\nByteswapIeee<double>: %.0f batches/s (%.1f ns/swap)\n", result.callsPerSecond,
              result.stats.median * 1000.0 / BATCH);
}

/* ----------------------------- Endianness Detection ----------------------------- */

/**
 * @brief Endianness detection is compile-time; verify zero runtime cost.
 */
PERF_TEST(Endian, DetectionOverhead) {
  UB_PERF_GUARD(perf);

  volatile bool sink = false;
  auto result = perf.throughputLoop(
      [&] { sink = (apex::compat::endian::native == apex::compat::endian::little); }, "detect");

  std::printf("\nEndian detection: %.0f ops/s (%.1f ns/op)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

PERF_MAIN()
