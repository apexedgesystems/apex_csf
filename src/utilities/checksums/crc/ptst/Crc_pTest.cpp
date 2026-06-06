/**
 * @file Crc_pTest.cpp
 * @brief Performance tests for CRC implementations.
 *
 * Measures:
 *  - Table vs Nibble vs Bitwise CRC-32 implementation throughput
 *  - CRC-32 Table throughput across payload sizes (cache effects)
 *  - CRC width comparison (8/16/32/64-bit) at fixed payload
 *  - Hardware (SSE4.2 / ARM CRC32) vs software CRC-32C
 *  - Memory bandwidth at large payloads
 *  - Streaming vs one-shot API and chunked update overhead
 *
 * Usage:
 *   ./Crc_PTEST --csv results.csv
 *   ./Crc_PTEST --quick
 *   ./Crc_PTEST --profile perf --gtest_filter="*Crc32*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace ub = vernier::bench;
namespace crc = apex::checksums::crc;

/* ----------------------------- Test Data ----------------------------- */

/**
 * @brief Generate random test data of specified size.
 */
static std::vector<uint8_t> generateTestData(size_t size) {
  std::vector<uint8_t> data(size);
  std::mt19937 gen(42); // Fixed seed for reproducibility
  std::uniform_int_distribution<unsigned int> dist(0, 255);
  for (auto& byte : data) {
    byte = static_cast<uint8_t>(dist(gen));
  }
  return data;
}

/* ----------------------------- Implementation Comparison ----------------------------- */

/**
 * @brief Compare Table vs Nibble vs Bitwise CRC-32 implementations.
 *
 * Tests with 4KB payload (fits in L1 cache) to isolate algorithm differences
 * from memory bandwidth effects.
 */
PERF_TEST(Crc32, ImplementationComparison) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 4096; // 4KB - fits in L1 cache
  auto data = generateTestData(PAYLOAD_SIZE);

  std::printf("\n=== CRC-32/ISO-HDLC Implementation Comparison (4KB payload) ===\n");

  // Table-driven (256 entries)
  crc::Crc32IsoHdlcTable tableCalc;
  volatile uint32_t sink = 0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      uint32_t out{};
      tableCalc.calculate(data, out);
      sink = out;
    }
  });

  auto tableResult = perf.throughputLoop(
      [&] {
        uint32_t out{};
        tableCalc.calculate(data, out);
        sink = out;
      },
      "table");

  double tableMBps = (PAYLOAD_SIZE * tableResult.callsPerSecond) / (1024.0 * 1024.0);
  std::printf("Table (256-entry):  %8.0f ops/s  %7.1f MB/s  (%.3f us/call)\n",
              tableResult.callsPerSecond, tableMBps, tableResult.stats.median);

  // Nibble-table (16 entries)
  crc::Crc32IsoHdlcNibble nibbleCalc;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      uint32_t out{};
      nibbleCalc.calculate(data, out);
      sink = out;
    }
  });

  auto nibbleResult = perf.throughputLoop(
      [&] {
        uint32_t out{};
        nibbleCalc.calculate(data, out);
        sink = out;
      },
      "nibble");

  double nibbleMBps = (PAYLOAD_SIZE * nibbleResult.callsPerSecond) / (1024.0 * 1024.0);
  std::printf("Nibble (16-entry):  %8.0f ops/s  %7.1f MB/s  (%.3f us/call)\n",
              nibbleResult.callsPerSecond, nibbleMBps, nibbleResult.stats.median);

  // Bitwise (no table)
  crc::Crc32IsoHdlcBitwise bitwiseCalc;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      uint32_t out{};
      bitwiseCalc.calculate(data, out);
      sink = out;
    }
  });

  auto bitwiseResult = perf.throughputLoop(
      [&] {
        uint32_t out{};
        bitwiseCalc.calculate(data, out);
        sink = out;
      },
      "bitwise");

  double bitwiseMBps = (PAYLOAD_SIZE * bitwiseResult.callsPerSecond) / (1024.0 * 1024.0);
  std::printf("Bitwise (no table): %8.0f ops/s  %7.1f MB/s  (%.3f us/call)\n",
              bitwiseResult.callsPerSecond, bitwiseMBps, bitwiseResult.stats.median);

  // Speedup analysis
  double tableVsBitwise = tableResult.callsPerSecond / bitwiseResult.callsPerSecond;
  double nibbleVsBitwise = nibbleResult.callsPerSecond / bitwiseResult.callsPerSecond;
  double tableVsNibble = tableResult.callsPerSecond / nibbleResult.callsPerSecond;

  std::printf("\nSpeedup vs Bitwise:\n");
  std::printf("  Table:  %.1fx faster\n", tableVsBitwise);
  std::printf("  Nibble: %.1fx faster\n", nibbleVsBitwise);
  std::printf("  Table vs Nibble: %.1fx\n", tableVsNibble);

  (void)sink;
}

/* ----------------------------- Payload Scaling ----------------------------- */

/**
 * @brief Test CRC-32 Table throughput across different payload sizes.
 *
 * Reveals cache hierarchy effects:
 *  - L1 cache: ~32KB, lowest latency
 *  - L2 cache: ~256KB, moderate latency
 *  - L3 cache: ~8MB, higher latency
 *  - RAM: >8MB, highest latency
 */
PERF_TEST(Crc32Table, PayloadScaling) {
  UB_PERF_GUARD(perf);

  struct TestCase {
    const char* name;
    size_t size;
  };

  std::vector<TestCase> testCases = {{"64B", 64},       {"256B", 256},   {"1KB", 1024},
                                     {"4KB", 4096},     {"16KB", 16384}, {"64KB", 65536},
                                     {"256KB", 262144}, {"1MB", 1048576}};

  std::printf("\n=== CRC-32/ISO-HDLC Table Payload Scaling ===\n");
  std::printf("%-8s %12s %12s %12s\n", "Size", "ops/s", "MB/s", "ns/byte");
  std::printf("%s\n", std::string(48, '-').c_str());

  crc::Crc32IsoHdlcTable calc;
  volatile uint32_t sink = 0;

  for (const auto& test : testCases) {
    auto data = generateTestData(test.size);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        uint32_t out{};
        calc.calculate(data, out);
        sink = out;
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          uint32_t out{};
          calc.calculate(data, out);
          sink = out;
        },
        test.name);

    double mbps = (test.size * result.callsPerSecond) / (1024.0 * 1024.0);
    double nsPerByte = (result.stats.median * 1000.0) / test.size;

    std::printf("%-8s %12.0f %12.1f %12.2f\n", test.name, result.callsPerSecond, mbps, nsPerByte);
  }

  (void)sink;
}

/* ----------------------------- CRC Width Comparison ----------------------------- */

/**
 * @brief Compare throughput across CRC widths (8/16/32/64-bit).
 *
 * All use Table implementation for fair comparison.
 * Wider CRCs may have slightly higher overhead due to larger registers.
 */
PERF_TEST(CrcWidth, TableComparison) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 4096; // 4KB
  auto data = generateTestData(PAYLOAD_SIZE);

  std::printf("\n=== CRC Width Comparison (Table, 4KB payload) ===\n");
  std::printf("%-20s %12s %12s\n", "Variant", "ops/s", "MB/s");
  std::printf("%s\n", std::string(48, '-').c_str());

  // CRC-8/SMBUS
  {
    crc::Crc8SmbusTable calc;
    volatile uint8_t sink = 0;

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        uint8_t out{};
        calc.calculate(data, out);
        sink = out;
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          uint8_t out{};
          calc.calculate(data, out);
          sink = out;
        },
        "crc8");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-20s %12.0f %12.1f\n", "CRC-8/SMBUS", result.callsPerSecond, mbps);
    (void)sink;
  }

  // CRC-16/MODBUS
  {
    crc::Crc16ModbusTable calc;
    volatile uint16_t sink = 0;

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        uint16_t out{};
        calc.calculate(data, out);
        sink = out;
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          uint16_t out{};
          calc.calculate(data, out);
          sink = out;
        },
        "crc16");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-20s %12.0f %12.1f\n", "CRC-16/MODBUS", result.callsPerSecond, mbps);
    (void)sink;
  }

  // CRC-32/ISO-HDLC
  {
    crc::Crc32IsoHdlcTable calc;
    volatile uint32_t sink = 0;

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        uint32_t out{};
        calc.calculate(data, out);
        sink = out;
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          uint32_t out{};
          calc.calculate(data, out);
          sink = out;
        },
        "crc32");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-20s %12.0f %12.1f\n", "CRC-32/ISO-HDLC", result.callsPerSecond, mbps);
    (void)sink;
  }

  // CRC-64/XZ
  {
    crc::Crc64XzTable calc;
    volatile uint64_t sink = 0;

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        uint64_t out{};
        calc.calculate(data, out);
        sink = out;
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          uint64_t out{};
          calc.calculate(data, out);
          sink = out;
        },
        "crc64");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-20s %12.0f %12.1f\n", "CRC-64/XZ", result.callsPerSecond, mbps);
    (void)sink;
  }
}

/* ----------------------------- Hardware Acceleration ----------------------------- */

/**
 * @brief Compare software vs hardware CRC-32C (ISCSI) implementations.
 *
 * Hardware uses SSE4.2 on x86 or CRC32 extension on ARM.
 * Software uses 256-entry table lookup.
 */
PERF_TEST(Crc32Iscsi, HardwareVsSoftware) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 4096;
  auto data = generateTestData(PAYLOAD_SIZE);

  std::printf("\n=== CRC-32C Hardware vs Software (4KB payload) ===\n");

#if APEX_CRC_HAS_SSE42
  std::printf("Hardware: SSE4.2\n");
#elif APEX_CRC_HAS_ARM_CRC32
  std::printf("Hardware: ARM CRC32\n");
#else
  std::printf("Hardware: Not available (using software fallback)\n");
#endif

  // Software (Table)
  crc::Crc32IscsiTable softwareCalc;
  volatile uint32_t sink = 0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      uint32_t out{};
      softwareCalc.calculate(data, out);
      sink = out;
    }
  });

  auto softwareResult = perf.throughputLoop(
      [&] {
        uint32_t out{};
        softwareCalc.calculate(data, out);
        sink = out;
      },
      "software");

  double softwareMBps = (PAYLOAD_SIZE * softwareResult.callsPerSecond) / (1024.0 * 1024.0);
  std::printf("Software (Table): %8.0f ops/s  %7.1f MB/s  (%.3f us/call)\n",
              softwareResult.callsPerSecond, softwareMBps, softwareResult.stats.median);

  // Hardware (or software fallback)
  crc::Crc32IscsiHardware hardwareCalc;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      uint32_t out{};
      hardwareCalc.calculate(data, out);
      sink = out;
    }
  });

  auto hardwareResult = perf.throughputLoop(
      [&] {
        uint32_t out{};
        hardwareCalc.calculate(data, out);
        sink = out;
      },
      "hardware");

  double hardwareMBps = (PAYLOAD_SIZE * hardwareResult.callsPerSecond) / (1024.0 * 1024.0);
  std::printf("Hardware:         %8.0f ops/s  %7.1f MB/s  (%.3f us/call)\n",
              hardwareResult.callsPerSecond, hardwareMBps, hardwareResult.stats.median);

  double speedup = hardwareResult.callsPerSecond / softwareResult.callsPerSecond;
  std::printf("Speedup: %.1fx\n", speedup);

#if APEX_CRC_HAS_HARDWARE
  // Hardware should be significantly faster
#endif

  (void)sink;
}

/**
 * @brief Test hardware CRC-32C with large payloads.
 *
 * Measures maximum achievable throughput with 16MB payload.
 */
PERF_TEST(Crc32Iscsi, HardwareThroughput) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = static_cast<const size_t>(16 * 1024 * 1024); // 16MB
  auto data = generateTestData(PAYLOAD_SIZE);

  crc::Crc32IscsiHardware calc;
  volatile uint32_t sink = 0;

  // Single warmup for large payload
  {
    uint32_t out{};
    calc.calculate(data, out);
    sink = out;
  }

  // Manual timing with fewer iterations
  const int REPS = 5;
  std::vector<double> latencies;
  latencies.reserve(REPS);

  for (int r = 0; r < REPS; ++r) {
    auto start = std::chrono::steady_clock::now();
    uint32_t out{};
    calc.calculate(data, out);
    sink = out;
    auto end = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(end - start).count();
    latencies.push_back(us);
  }

  std::sort(latencies.begin(), latencies.end());
  double median = latencies[latencies.size() / 2];
  double callsPerSec = 1'000'000.0 / median;
  double mbps = (PAYLOAD_SIZE * callsPerSec) / (1024.0 * 1024.0);
  double gbps = mbps / 1024.0;

  std::printf("\n=== CRC-32C Hardware Throughput (16MB payload) ===\n");
#if APEX_CRC_HAS_SSE42
  std::printf("Using: SSE4.2\n");
#elif APEX_CRC_HAS_ARM_CRC32
  std::printf("Using: ARM CRC32\n");
#else
  std::printf("Using: Software (no hardware available)\n");
#endif
  std::printf("Throughput: %.1f MB/s (%.2f GB/s)\n", mbps, gbps);
  std::printf("Latency: %.2f ms per 16MB\n", median / 1000.0);

  (void)sink;
}

/* ----------------------------- Memory Bandwidth ----------------------------- */

/**
 * @brief Measure CRC-32 Table throughput with large payloads.
 *
 * Tests if we're CPU-bound or memory-bound with 1MB+ payloads.
 */
PERF_TEST(Crc32Table, MemoryBandwidth) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE =
      static_cast<const size_t>(4 * 1024 * 1024); // 4MB - large enough to exceed L3
  auto data = generateTestData(PAYLOAD_SIZE);

  crc::Crc32IsoHdlcTable calc;
  volatile uint32_t sink = 0;

  // Single warmup iteration for large payload
  {
    uint32_t out{};
    calc.calculate(data, out);
    sink = out;
  }

  // Manual timing with fewer iterations for large payloads
  const int REPS = 5;
  std::vector<double> latencies;
  latencies.reserve(REPS);

  for (int r = 0; r < REPS; ++r) {
    auto start = std::chrono::steady_clock::now();
    uint32_t out{};
    calc.calculate(data, out);
    sink = out;
    auto end = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(end - start).count();
    latencies.push_back(us);
  }

  std::sort(latencies.begin(), latencies.end());
  double median = latencies[latencies.size() / 2];
  double callsPerSec = 1'000'000.0 / median;
  double mbps = (PAYLOAD_SIZE * callsPerSec) / (1024.0 * 1024.0);

  std::printf("\n=== CRC-32 Table Memory Bandwidth (4MB payload) ===\n");
  std::printf("Throughput: %.1f MB/s\n", mbps);
  std::printf("Latency: %.1f ms per 4MB\n", median / 1000.0);

  // Expect reasonable bandwidth (>100 MB/s on any modern CPU)

  (void)sink;
}

/* ----------------------------- Streaming API ----------------------------- */

/**
 * @brief Compare one-shot vs streaming API performance.
 *
 * Tests if the streaming API (reset/update/finalize) has measurable overhead
 * compared to one-shot calculate().
 */
PERF_TEST(Crc32Table, StreamingVsOneshot) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 4096;
  auto data = generateTestData(PAYLOAD_SIZE);

  crc::Crc32IsoHdlcTable calc;
  volatile uint32_t sink = 0;

  std::printf("\n=== Streaming vs One-shot API (4KB payload) ===\n");

  // One-shot
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      uint32_t out{};
      calc.calculate(data, out);
      sink = out;
    }
  });

  auto oneshotResult = perf.throughputLoop(
      [&] {
        uint32_t out{};
        calc.calculate(data, out);
        sink = out;
      },
      "oneshot");

  std::printf("One-shot:  %.0f ops/s (%.3f us/call)\n", oneshotResult.callsPerSecond,
              oneshotResult.stats.median);

  // Streaming (single update)
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      calc.reset();
      calc.update(data.data(), data.size());
      uint32_t out{};
      calc.finalize(out);
      sink = out;
    }
  });

  auto streamResult = perf.throughputLoop(
      [&] {
        calc.reset();
        calc.update(data.data(), data.size());
        uint32_t out{};
        calc.finalize(out);
        sink = out;
      },
      "streaming");

  std::printf("Streaming: %.0f ops/s (%.3f us/call)\n", streamResult.callsPerSecond,
              streamResult.stats.median);

  double overhead =
      ((streamResult.stats.median - oneshotResult.stats.median) / oneshotResult.stats.median) *
      100.0;
  std::printf("Streaming overhead: %.1f%%\n", overhead);

  // Streaming should be within 20% of one-shot (allow for measurement noise)

  (void)sink;
}

/* ----------------------------- Incremental Updates ----------------------------- */

/**
 * @brief Test streaming with multiple small chunks.
 *
 * Simulates processing data in chunks (e.g., network packets).
 */
PERF_TEST(Crc32Table, ChunkedUpdates) {
  UB_PERF_GUARD(perf);

  const size_t TOTAL_SIZE = 4096;
  const size_t CHUNK_SIZE = 64; // Typical MTU fragment
  auto data = generateTestData(TOTAL_SIZE);
  const size_t NUM_CHUNKS = TOTAL_SIZE / CHUNK_SIZE;

  crc::Crc32IsoHdlcTable calc;
  volatile uint32_t sink = 0;

  std::printf("\n=== Chunked Updates (4KB as %zu x 64B chunks) ===\n", NUM_CHUNKS);

  // Chunked
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      calc.reset();
      for (size_t j = 0; j < NUM_CHUNKS; ++j) {
        calc.update(data.data() + j * CHUNK_SIZE, CHUNK_SIZE);
      }
      uint32_t out{};
      calc.finalize(out);
      sink = out;
    }
  });

  auto chunkedResult = perf.throughputLoop(
      [&] {
        calc.reset();
        for (size_t j = 0; j < NUM_CHUNKS; ++j) {
          calc.update(data.data() + j * CHUNK_SIZE, CHUNK_SIZE);
        }
        uint32_t out{};
        calc.finalize(out);
        sink = out;
      },
      "chunked");

  double mbps = (TOTAL_SIZE * chunkedResult.callsPerSecond) / (1024.0 * 1024.0);
  std::printf("Chunked:  %.0f ops/s  %.1f MB/s (%.3f us/call)\n", chunkedResult.callsPerSecond,
              mbps, chunkedResult.stats.median);

  // One-shot for comparison
  auto oneshotResult = perf.throughputLoop(
      [&] {
        uint32_t out{};
        calc.calculate(data, out);
        sink = out;
      },
      "oneshot");

  double oneshotMbps = (TOTAL_SIZE * oneshotResult.callsPerSecond) / (1024.0 * 1024.0);
  std::printf("One-shot: %.0f ops/s  %.1f MB/s (%.3f us/call)\n", oneshotResult.callsPerSecond,
              oneshotMbps, oneshotResult.stats.median);

  double slowdown = oneshotResult.callsPerSecond / chunkedResult.callsPerSecond;
  std::printf("Chunking overhead: %.1fx slower\n", slowdown);

  (void)sink;
}

PERF_MAIN()
