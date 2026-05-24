/**
 * @file Helpers_pTest.cpp
 * @brief Performance tests for helper utilities.
 *
 * Measures:
 *  - Byte LE/BE load throughput across type widths
 *  - Hex encode/decode throughput
 *  - Bit manipulation throughput (set, clear, flip, test)
 *
 * Usage:
 *   ./Helpers_PTEST --csv results.csv
 *   ./Helpers_PTEST --quick
 *   ./Helpers_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/helpers/inc/Bits.hpp"
#include "src/utilities/helpers/inc/Bytes.hpp"
#include "src/utilities/helpers/inc/Strings.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace ub = vernier::bench;

/* ----------------------------- Test Data ----------------------------- */

static std::vector<uint8_t> generateBytes(size_t count, unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::uniform_int_distribution<unsigned int> dist(0, 255);
  std::vector<uint8_t> data(count);
  for (auto& b : data) {
    b = static_cast<uint8_t>(dist(gen));
  }
  return data;
}

/* ----------------------------- Byte Load/Store ----------------------------- */

/**
 * @brief Compare LE/BE load throughput across type widths.
 *
 * Batch of 10000 loads to amortize loop overhead.
 */
PERF_TEST(ByteOps, LoadComparison) {
  UB_PERF_GUARD(perf);

  constexpr size_t BATCH = 10000;
  auto data = generateBytes(BATCH * 8); // 8 bytes per element (max width)

  std::printf("\n=== Byte Load Comparison (batch of %zu) ===\n", BATCH);

  // LE 32-bit
  {
    volatile uint32_t sink = 0;
    auto result = perf.throughputLoop(
        [&] {
          for (size_t i = 0; i < BATCH; ++i) {
            sink = apex::helpers::bytes::loadLe<uint32_t>(&data[i * 4]);
          }
        },
        "le32");

    std::printf("loadLe<u32>: %.0f batches/s (%.1f ns/load)\n", result.callsPerSecond,
                result.stats.median * 1000.0 / BATCH);
  }

  // BE 32-bit
  {
    volatile uint32_t sink = 0;
    auto result = perf.throughputLoop(
        [&] {
          for (size_t i = 0; i < BATCH; ++i) {
            sink = apex::helpers::bytes::loadBe<uint32_t>(&data[i * 4]);
          }
        },
        "be32");

    std::printf("loadBe<u32>: %.0f batches/s (%.1f ns/load)\n", result.callsPerSecond,
                result.stats.median * 1000.0 / BATCH);
  }

  // LE 64-bit
  {
    volatile uint64_t sink = 0;
    auto result = perf.throughputLoop(
        [&] {
          for (size_t i = 0; i < BATCH; ++i) {
            sink = apex::helpers::bytes::loadLe<uint64_t>(&data[i * 8]);
          }
        },
        "le64");

    std::printf("loadLe<u64>: %.0f batches/s (%.1f ns/load)\n", result.callsPerSecond,
                result.stats.median * 1000.0 / BATCH);
  }
}

/* ----------------------------- Hex Encoding ----------------------------- */

/**
 * @brief Hex encoding/decoding throughput.
 *
 * Tests toHexBuffer and fromHexBuffer on 1KB payload.
 */
PERF_TEST(HexOps, EncodeDecode) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD = 1024;
  auto data = generateBytes(PAYLOAD);
  std::array<char, PAYLOAD * 2 + 1> hexBuf{};
  std::array<uint8_t, PAYLOAD> decodeBuf{};

  std::printf("\n=== Hex Encode/Decode (1KB payload) ===\n");

  // Encode
  {
    auto result = perf.throughputLoop(
        [&] { apex::helpers::strings::toHexArray(data.data(), data.size(), hexBuf); }, "encode");

    double mbps = (PAYLOAD * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("toHexBuffer: %.0f ops/s  %.1f MB/s\n", result.callsPerSecond, mbps);
  }

  // Ensure valid hex for decode
  apex::helpers::strings::toHexArray(data.data(), data.size(), hexBuf);

  // Decode
  {
    auto result = perf.throughputLoop(
        [&] {
          apex::helpers::strings::fromHexBuffer(hexBuf.data(), decodeBuf.data(), decodeBuf.size());
        },
        "decode");

    double mbps = (PAYLOAD * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("fromHexBuffer: %.0f ops/s  %.1f MB/s\n", result.callsPerSecond, mbps);
  }
}

/* ----------------------------- Bit Manipulation ----------------------------- */

/**
 * @brief Bit manipulation throughput (set/clear/flip/test).
 *
 * Batch of 10000 operations.
 */
PERF_TEST(BitOps, Throughput) {
  UB_PERF_GUARD(perf);

  constexpr size_t BATCH = 10000;
  std::vector<uint8_t> bytes(BATCH, 0);

  volatile bool sink = false;
  auto result = perf.throughputLoop(
      [&] {
        for (size_t i = 0; i < BATCH; ++i) {
          apex::helpers::bits::set(bytes[i], static_cast<uint8_t>(i & 7));
          apex::helpers::bits::flip(bytes[i], static_cast<uint8_t>((i + 1) & 7));
          sink = apex::helpers::bits::test(bytes[i], static_cast<uint8_t>((i + 2) & 7));
          apex::helpers::bits::clear(bytes[i], static_cast<uint8_t>(i & 7));
        }
      },
      "set-flip-test-clear");

  std::printf("\nBitOps (set+flip+test+clear): %.0f batches/s (%.1f ns/4-op-cycle)\n",
              result.callsPerSecond, result.stats.median * 1000.0 / BATCH);
}

PERF_MAIN()
