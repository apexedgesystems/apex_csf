/**
 * @file UartAdapter_pTest.cpp
 * @brief Performance tests for UART adapter I/O via PTY loopback.
 *
 * Measures:
 *  - Write/read throughput (clean, burst, small, large)
 *  - Loopback round-trip throughput
 *  - Payload size scaling (8 B - 64 KB)
 *  - Write/read latency distribution
 *  - Configure and flush overhead
 *
 * Usage:
 *   ./UartAdapter_PTEST --csv results.csv
 *   ./UartAdapter_PTEST --quick
 *   ./UartAdapter_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartConfig.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartStatus.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ub = vernier::bench;
namespace uart = apex::protocols::serial::uart;

/* ----------------------------- Local Helpers ----------------------------- */

namespace {

inline const ub::PerfConfig& getCfg() { return ub::detail::getPerfConfig(); }

inline std::vector<std::uint8_t> makePayload(std::size_t n) {
  std::vector<std::uint8_t> payload(n);
  for (std::size_t i = 0; i < n; ++i) {
    payload[i] = static_cast<std::uint8_t>(i & 0xFF);
  }
  return payload;
}

inline void drainPty(uart::PtyPair& pty) {
  std::array<std::uint8_t, 4096> buf{};
  std::size_t bytesRead = 0;
  while (pty.readMaster(buf.data(), buf.size(), bytesRead, 0) == uart::Status::SUCCESS) {
    if (bytesRead == 0)
      break;
  }
}

} // namespace

/* ----------------------------- Write Throughput ----------------------------- */

/**
 * @brief Raw write throughput to PTY.
 */
PERF_TEST(UartThroughput, WriteClean) {
  UB_PERF_GUARD(perf);

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makePayload(PAYLOAD_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  perf.setup([&] {
    std::size_t written = 0;
    auto s = adapter.write(payload.data(), payload.size(), written, 100);
    ASSERT_EQ(s, uart::Status::SUCCESS);
    ASSERT_EQ(written, payload.size());
    drainPty(pty);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      volatile auto s = adapter.write(payload.data(), payload.size(), written, 100);
      (void)s;
      drainPty(pty);
    }
  });

  ub::MemoryProfile memProfile{.bytesRead = 0, .bytesWritten = PAYLOAD_SIZE, .bytesAllocated = 0};

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto s = adapter.write(payload.data(), payload.size(), written, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
        drainPty(pty);
      },
      "write-clean", memProfile);

  const double MB_PER_SEC = (R.callsPerSecond * PAYLOAD_SIZE) / (1024.0 * 1024.0);
  std::printf("\nWrite throughput: %.1f MB/s (%.0f calls/s)\n", MB_PER_SEC, R.callsPerSecond);
}

/**
 * @brief Raw read throughput from PTY.
 */
PERF_TEST(UartThroughput, ReadClean) {
  UB_PERF_GUARD(perf);

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makePayload(PAYLOAD_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  std::vector<std::uint8_t> readBuf(PAYLOAD_SIZE);

  perf.setup([&] {
    std::size_t written = 0;
    ASSERT_EQ(pty.writeMaster(payload.data(), payload.size(), written, 100), uart::Status::SUCCESS);
    std::size_t bytesRead = 0;
    auto s = adapter.read(readBuf.data(), readBuf.size(), bytesRead, 100);
    ASSERT_EQ(s, uart::Status::SUCCESS);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      (void)pty.writeMaster(payload.data(), payload.size(), written, 100);
      std::size_t bytesRead = 0;
      volatile auto s = adapter.read(readBuf.data(), readBuf.size(), bytesRead, 100);
      (void)s;
    }
  });

  ub::MemoryProfile memProfile{.bytesRead = PAYLOAD_SIZE, .bytesWritten = 0, .bytesAllocated = 0};

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        (void)pty.writeMaster(payload.data(), payload.size(), written, 100);
        std::size_t bytesRead = 0;
        auto s = adapter.read(readBuf.data(), readBuf.size(), bytesRead, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
      },
      "read-clean", memProfile);

  const double MB_PER_SEC = (R.callsPerSecond * PAYLOAD_SIZE) / (1024.0 * 1024.0);
  std::printf("\nRead throughput: %.1f MB/s (%.0f calls/s)\n", MB_PER_SEC, R.callsPerSecond);
}

/**
 * @brief Full round-trip throughput (write + read).
 */
PERF_TEST(UartThroughput, LoopbackRoundTrip) {
  UB_PERF_GUARD(perf);

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makePayload(PAYLOAD_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  std::vector<std::uint8_t> readBuf(PAYLOAD_SIZE);

  perf.setup([&] {
    std::size_t written = 0;
    ASSERT_EQ(adapter.write(payload.data(), payload.size(), written, 100), uart::Status::SUCCESS);

    std::size_t masterRead = 0;
    ASSERT_EQ(pty.readMaster(readBuf.data(), readBuf.size(), masterRead, 100),
              uart::Status::SUCCESS);

    std::size_t masterWritten = 0;
    ASSERT_EQ(pty.writeMaster(readBuf.data(), masterRead, masterWritten, 100),
              uart::Status::SUCCESS);

    std::size_t adapterRead = 0;
    ASSERT_EQ(adapter.read(readBuf.data(), readBuf.size(), adapterRead, 100),
              uart::Status::SUCCESS);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      (void)adapter.write(payload.data(), payload.size(), written, 100);

      std::size_t masterRead = 0;
      (void)pty.readMaster(readBuf.data(), readBuf.size(), masterRead, 100);

      std::size_t masterWritten = 0;
      (void)pty.writeMaster(readBuf.data(), masterRead, masterWritten, 100);

      std::size_t adapterRead = 0;
      volatile auto s = adapter.read(readBuf.data(), readBuf.size(), adapterRead, 100);
      (void)s;
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = PAYLOAD_SIZE, .bytesWritten = PAYLOAD_SIZE, .bytesAllocated = 0};

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        (void)adapter.write(payload.data(), payload.size(), written, 100);

        std::size_t masterRead = 0;
        (void)pty.readMaster(readBuf.data(), readBuf.size(), masterRead, 100);

        std::size_t masterWritten = 0;
        (void)pty.writeMaster(readBuf.data(), masterRead, masterWritten, 100);

        std::size_t adapterRead = 0;
        auto s = adapter.read(readBuf.data(), readBuf.size(), adapterRead, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
      },
      "loopback-roundtrip", memProfile);

  const double MB_PER_SEC = (R.callsPerSecond * PAYLOAD_SIZE) / (1024.0 * 1024.0);
  std::printf("\nRound-trip throughput: %.1f MB/s (%.0f round-trips/s)\n", MB_PER_SEC,
              R.callsPerSecond);
}

/**
 * @brief Burst write performance (multiple consecutive writes).
 */
PERF_TEST(UartThroughput, WriteBurst) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t BURST_SIZE = 8;
  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makePayload(PAYLOAD_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (std::size_t b = 0; b < BURST_SIZE; ++b) {
        std::size_t written = 0;
        (void)adapter.write(payload.data(), payload.size(), written, 100);
      }
      drainPty(pty);
    }
  });

  const std::size_t TOTAL_BYTES = PAYLOAD_SIZE * BURST_SIZE;
  ub::MemoryProfile memProfile{.bytesRead = 0, .bytesWritten = TOTAL_BYTES, .bytesAllocated = 0};

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        for (std::size_t b = 0; b < BURST_SIZE; ++b) {
          std::size_t written = 0;
          auto s = adapter.write(payload.data(), payload.size(), written, 100);
          ASSERT_EQ(s, uart::Status::SUCCESS);
        }
        drainPty(pty);
      },
      "write-burst", memProfile);

  const double MB_PER_SEC = (R.callsPerSecond * TOTAL_BYTES) / (1024.0 * 1024.0);
  std::printf("\nBurst write throughput: %.1f MB/s (%zu writes/burst)\n", MB_PER_SEC, BURST_SIZE);
}

/**
 * @brief Overhead of small writes (syscall-dominated).
 */
PERF_TEST(UartThroughput, SmallWrites) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t SMALL_SIZE = 8;
  const auto payload = makePayload(SMALL_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      (void)adapter.write(payload.data(), payload.size(), written, 100);
      drainPty(pty);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto s = adapter.write(payload.data(), payload.size(), written, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
        drainPty(pty);
      },
      "write-small");

  std::printf("\nSmall write (%zuB): %.3f us/call (%.0f calls/s)\n", SMALL_SIZE, R.stats.median,
              R.callsPerSecond);
}

/**
 * @brief Throughput with large (16 KB) payloads.
 */
PERF_TEST(UartThroughput, LargeWrites) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t LARGE_SIZE = 16 * 1024;
  const auto payload = makePayload(LARGE_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles() / 10; ++i) {
      std::size_t written = 0;
      (void)adapter.write(payload.data(), payload.size(), written, 100);
      drainPty(pty);
    }
  });

  ub::MemoryProfile memProfile{.bytesRead = 0, .bytesWritten = LARGE_SIZE, .bytesAllocated = 0};

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto s = adapter.write(payload.data(), payload.size(), written, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
        drainPty(pty);
      },
      "write-large", memProfile);

  const double MB_PER_SEC = (R.callsPerSecond * LARGE_SIZE) / (1024.0 * 1024.0);
  std::printf("\nLarge write (%zuKB): %.1f MB/s\n", LARGE_SIZE / 1024, MB_PER_SEC);
}

/* ----------------------------- Payload Size Sweep ----------------------------- */

class UartPayloadSweep : public ::testing::TestWithParam<std::size_t> {
protected:
};

/**
 * @brief Write throughput across multiple payload sizes.
 */
TEST_P(UartPayloadSweep, WriteSweep) {
  const std::size_t PAYLOAD_SIZE = GetParam();

  ub::PerfConfig cfg = getCfg();
  cfg.msgBytes = static_cast<int>(PAYLOAD_SIZE);

  std::string testName = "UartPayloadSweep.WriteSweep/" + std::to_string(PAYLOAD_SIZE);
  auto perf = ub::makePerfCaseWithProfiler(testName, cfg);

  const auto payload = makePayload(PAYLOAD_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig uartCfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(uartCfg), uart::Status::SUCCESS);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      (void)adapter.write(payload.data(), payload.size(), written, 100);
      drainPty(pty);
    }
  });

  ub::MemoryProfile memProfile{.bytesRead = 0, .bytesWritten = PAYLOAD_SIZE, .bytesAllocated = 0};

  const auto R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto s = adapter.write(payload.data(), payload.size(), written, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
        drainPty(pty);
      },
      "write-sweep", memProfile);
}

/**
 * @brief Read throughput across multiple payload sizes.
 */
TEST_P(UartPayloadSweep, ReadSweep) {
  const std::size_t PAYLOAD_SIZE = GetParam();

  ub::PerfConfig cfg = getCfg();
  cfg.msgBytes = static_cast<int>(PAYLOAD_SIZE);

  std::string testName = "UartPayloadSweep.ReadSweep/" + std::to_string(PAYLOAD_SIZE);
  auto perf = ub::makePerfCaseWithProfiler(testName, cfg);

  const auto payload = makePayload(PAYLOAD_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig uartCfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(uartCfg), uart::Status::SUCCESS);

  std::vector<std::uint8_t> readBuf(PAYLOAD_SIZE);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      (void)pty.writeMaster(payload.data(), payload.size(), written, 100);
      std::size_t bytesRead = 0;
      (void)adapter.read(readBuf.data(), readBuf.size(), bytesRead, 100);
    }
  });

  ub::MemoryProfile memProfile{.bytesRead = PAYLOAD_SIZE, .bytesWritten = 0, .bytesAllocated = 0};

  const auto R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        (void)pty.writeMaster(payload.data(), payload.size(), written, 100);
        std::size_t bytesRead = 0;
        auto s = adapter.read(readBuf.data(), readBuf.size(), bytesRead, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
      },
      "read-sweep", memProfile);
}

INSTANTIATE_TEST_SUITE_P(PayloadSizes, UartPayloadSweep,
                         ::testing::Values(8, 64, 256, 1024, 4096, 16384, 65536));

/* ----------------------------- Latency ----------------------------- */

/**
 * @brief Write latency distribution (p10, p50, p90).
 */
PERF_TEST(UartLatency, WriteDistribution) {
  UB_PERF_GUARD(perf);

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makePayload(PAYLOAD_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      (void)adapter.write(payload.data(), payload.size(), written, 100);
      drainPty(pty);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto s = adapter.write(payload.data(), payload.size(), written, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
        drainPty(pty);
      },
      "write-latency");

  std::printf("\nWrite Latency Distribution (%zuB):\n", PAYLOAD_SIZE);
  std::printf("  p10: %.3f us (best case)\n", R.stats.p10);
  std::printf("  p50: %.3f us (median)\n", R.stats.median);
  std::printf("  p90: %.3f us (worst case)\n", R.stats.p90);
  std::printf("  p90/p50 ratio: %.2f\n", R.stats.p90 / R.stats.median);
  std::printf("  CV: %.1f%%\n", R.stats.cv * 100.0);
}

/**
 * @brief Read latency distribution.
 */
PERF_TEST(UartLatency, ReadDistribution) {
  UB_PERF_GUARD(perf);

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makePayload(PAYLOAD_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  std::vector<std::uint8_t> readBuf(PAYLOAD_SIZE);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      (void)pty.writeMaster(payload.data(), payload.size(), written, 100);
      std::size_t bytesRead = 0;
      (void)adapter.read(readBuf.data(), readBuf.size(), bytesRead, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        (void)pty.writeMaster(payload.data(), payload.size(), written, 100);
        std::size_t bytesRead = 0;
        auto s = adapter.read(readBuf.data(), readBuf.size(), bytesRead, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
      },
      "read-latency");

  std::printf("\nRead Latency Distribution (%zuB):\n", PAYLOAD_SIZE);
  std::printf("  p10: %.3f us (best case)\n", R.stats.p10);
  std::printf("  p50: %.3f us (median)\n", R.stats.median);
  std::printf("  p90: %.3f us (worst case)\n", R.stats.p90);
  std::printf("  p90/p50 ratio: %.2f\n", R.stats.p90 / R.stats.median);
  std::printf("  CV: %.1f%%\n", R.stats.cv * 100.0);
}

/**
 * @brief Full round-trip latency distribution.
 */
PERF_TEST(UartLatency, RoundTrip) {
  UB_PERF_GUARD(perf);

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makePayload(PAYLOAD_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  std::vector<std::uint8_t> readBuf(PAYLOAD_SIZE);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      (void)adapter.write(payload.data(), payload.size(), written, 100);
      std::size_t masterRead = 0;
      (void)pty.readMaster(readBuf.data(), readBuf.size(), masterRead, 100);
      std::size_t masterWritten = 0;
      (void)pty.writeMaster(readBuf.data(), masterRead, masterWritten, 100);
      std::size_t adapterRead = 0;
      (void)adapter.read(readBuf.data(), readBuf.size(), adapterRead, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        (void)adapter.write(payload.data(), payload.size(), written, 100);
        std::size_t masterRead = 0;
        (void)pty.readMaster(readBuf.data(), readBuf.size(), masterRead, 100);
        std::size_t masterWritten = 0;
        (void)pty.writeMaster(readBuf.data(), masterRead, masterWritten, 100);
        std::size_t adapterRead = 0;
        auto s = adapter.read(readBuf.data(), readBuf.size(), adapterRead, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
      },
      "roundtrip-latency");

  std::printf("\nRound-Trip Latency (%zuB):\n", PAYLOAD_SIZE);
  std::printf("  p10: %.3f us\n", R.stats.p10);
  std::printf("  p50: %.3f us (median)\n", R.stats.median);
  std::printf("  p90: %.3f us\n", R.stats.p90);
  std::printf("  Throughput: %.0f round-trips/s\n", R.callsPerSecond);
}

/* ----------------------------- Configuration ----------------------------- */

/**
 * @brief Configuration overhead (termios setup via tcsetattr).
 */
PERF_TEST(UartConfig, ConfigureOverhead) {
  UB_PERF_GUARD(perf);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      uart::UartAdapter adapter(pty.slavePath());
      volatile auto s = adapter.configure(cfg);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        uart::UartAdapter adapter(pty.slavePath());
        auto s = adapter.configure(cfg);
        ASSERT_EQ(s, uart::Status::SUCCESS);
      },
      "configure");

  std::printf("\nConfigure overhead: %.3f us (%.0f configs/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief Flush overhead.
 */
PERF_TEST(UartConfig, FlushOverhead) {
  UB_PERF_GUARD(perf);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto s = adapter.flush(true, true);
      (void)s;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto s = adapter.flush(true, true);
        ASSERT_EQ(s, uart::Status::SUCCESS);
      },
      "flush");

  std::printf("\nFlush overhead: %.3f us (%.0f flushes/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- Write vs Read Comparison ----------------------------- */

/**
 * @brief Direct comparison of write and read performance.
 */
PERF_TEST(UartComparison, WriteVsRead) {
  UB_PERF_GUARD(perf);

  const std::size_t PAYLOAD_SIZE = static_cast<std::size_t>(getCfg().msgBytes);
  const auto payload = makePayload(PAYLOAD_SIZE);

  uart::PtyPair pty;
  ASSERT_EQ(pty.open(), uart::Status::SUCCESS);

  uart::UartConfig cfg;
  uart::UartAdapter adapter(pty.slavePath());
  ASSERT_EQ(adapter.configure(cfg), uart::Status::SUCCESS);

  std::vector<std::uint8_t> readBuf(PAYLOAD_SIZE);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      (void)adapter.write(payload.data(), payload.size(), written, 100);
      drainPty(pty);
    }
  });

  const auto writeResult = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto s = adapter.write(payload.data(), payload.size(), written, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
        drainPty(pty);
      },
      "write");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t written = 0;
      (void)pty.writeMaster(payload.data(), payload.size(), written, 100);
      std::size_t bytesRead = 0;
      (void)adapter.read(readBuf.data(), readBuf.size(), bytesRead, 100);
    }
  });

  const auto readResult = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        (void)pty.writeMaster(payload.data(), payload.size(), written, 100);
        std::size_t bytesRead = 0;
        auto s = adapter.read(readBuf.data(), readBuf.size(), bytesRead, 100);
        ASSERT_EQ(s, uart::Status::SUCCESS);
      },
      "read");

  const double writeMBps = (writeResult.callsPerSecond * PAYLOAD_SIZE) / (1024.0 * 1024.0);
  const double readMBps = (readResult.callsPerSecond * PAYLOAD_SIZE) / (1024.0 * 1024.0);
  const double ratio = writeResult.stats.median / readResult.stats.median;

  std::printf("\n=== Write vs Read Comparison (%zuB) ===\n", PAYLOAD_SIZE);
  std::printf("\nWrite:\n");
  std::printf("  Latency:    %.3f us\n", writeResult.stats.median);
  std::printf("  Throughput: %.1f MB/s\n", writeMBps);
  std::printf("  CV:         %.1f%%\n", writeResult.stats.cv * 100.0);

  std::printf("\nRead:\n");
  std::printf("  Latency:    %.3f us\n", readResult.stats.median);
  std::printf("  Throughput: %.1f MB/s\n", readMBps);
  std::printf("  CV:         %.1f%%\n", readResult.stats.cv * 100.0);

  std::printf("\nRatio:\n");
  std::printf("  Write/Read: %.2fx\n", ratio);
  if (ratio > 1.0) {
    std::printf("  Read is %.1f%% faster\n", ((ratio - 1.0) * 100.0));
  } else {
    std::printf("  Write is %.1f%% faster\n", ((1.0 / ratio - 1.0) * 100.0));
  }
}

PERF_MAIN()
