/**
 * @file SystemLog_pTest.cpp
 * @brief Performance tests for SystemLog SYNC/ASYNC log paths.
 *
 * Measures:
 *  - SYNC mode blocking write throughput (O_APPEND)
 *  - ASYNC mode lock-free queue throughput (RT-safe)
 *  - Below-threshold skip path latency
 *  - Single-thread and multi-thread contention
 *
 * Usage:
 *   ./SystemLog_PTEST --csv results.csv
 *   ./SystemLog_PTEST --quick
 *   ./SystemLog_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace ub = vernier::bench;

using logs::Status;
using logs::SystemLog;

/* ----------------------------- Test Helpers ----------------------------- */

namespace {

inline const ub::PerfConfig& getCfg() { return ub::detail::getPerfConfig(); }

inline std::string makeMsg(std::size_t n) {
  std::string msg(n, '\0');
  for (std::size_t i = 0; i < n; ++i) {
    msg[i] = static_cast<char>('a' + (i % 26));
  }
  return msg;
}

struct TempFileGuard {
  std::filesystem::path p;
  ~TempFileGuard() {
    std::error_code ec;
    std::filesystem::remove(p, ec);
  }
};

} // namespace

/* ----------------------------- Sync Mode ----------------------------- */

/**
 * @brief Single-threaded SYNC mode write throughput (blocking O_APPEND).
 */
PERF_TEST(SyncMode, SingleThreadThroughput) {
  UB_PERF_GUARD(perf);

  const auto PATH = ub::uniqTempFile("logs_sync_st");
  TempFileGuard cleanup{PATH};
  const std::string MSG = makeMsg(static_cast<std::size_t>(getCfg().msgBytes));

  SystemLog log(PATH.string(), SystemLog::Mode::SYNC);
  log.setLevel(SystemLog::Level::INFO);

  perf.setup([&] { ASSERT_EQ(log.lastOpenStatus(), Status::OK); });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)log.info("PERF", MSG, false);
    }
  });

  auto result = perf.throughputLoop([&] { ASSERT_EQ(log.info("PERF", MSG, false), Status::OK); },
                                    "sync-info");

  std::printf("\nSYNC single-thread: %.0f ops/s (%.1f us/op)\n", result.callsPerSecond,
              result.stats.median);
}

/**
 * @brief Multi-threaded SYNC mode contention (O_APPEND atomicity).
 */
PERF_CONTENTION(SyncMode, MultiThreadContention) {
  UB_PERF_GUARD(perf);

  const auto PATH = ub::uniqTempFile("logs_sync_mt");
  TempFileGuard cleanup{PATH};
  const std::string MSG = makeMsg(static_cast<std::size_t>(getCfg().msgBytes));

  SystemLog log(PATH.string(), SystemLog::Mode::SYNC);
  log.setLevel(SystemLog::Level::INFO);

  perf.setup([&] {
    ASSERT_EQ(log.lastOpenStatus(), Status::OK);
    for (int i = 0; i < 1000; ++i) {
      (void)log.info("WARM", "warmup", false);
    }
  });

  auto result = perf.contentionRun([&] { (void)log.info("MT", MSG, false); }, "sync-mt");

  std::printf("\nSYNC contended (%d threads): %.0f ops/s (%.1f us/op)\n", perf.threads(),
              result.callsPerSecond, result.stats.median);
}

/* ----------------------------- Async Mode ----------------------------- */

/**
 * @brief Single-threaded ASYNC mode throughput (lock-free queue push).
 */
PERF_TEST(AsyncMode, SingleThreadThroughput) {
  UB_PERF_GUARD(perf);

  const auto PATH = ub::uniqTempFile("logs_async_st");
  TempFileGuard cleanup{PATH};
  const std::string MSG = makeMsg(static_cast<std::size_t>(getCfg().msgBytes));

  SystemLog log(PATH.string(), SystemLog::Mode::ASYNC, 8192);
  log.setLevel(SystemLog::Level::INFO);

  perf.setup([&] {
    ASSERT_EQ(log.lastOpenStatus(), Status::OK);
    ASSERT_TRUE(log.isAsync()) << "ASYNC mode failed to start";
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)log.info("PERF", MSG, false);
    }
    log.flush();
  });

  auto result = perf.throughputLoop([&] { ASSERT_EQ(log.info("PERF", MSG, false), Status::OK); },
                                    "async-info");

  std::printf("\nASYNC single-thread: %.0f ops/s (%.1f us/op)\n", result.callsPerSecond,
              result.stats.median);
}

/**
 * @brief Multi-threaded ASYNC mode contention (lock-free queue under producers).
 */
PERF_CONTENTION(AsyncMode, MultiThreadContention) {
  UB_PERF_GUARD(perf);

  const auto PATH = ub::uniqTempFile("logs_async_mt");
  TempFileGuard cleanup{PATH};
  const std::string MSG = makeMsg(static_cast<std::size_t>(getCfg().msgBytes));

  SystemLog log(PATH.string(), SystemLog::Mode::ASYNC, 16384);
  log.setLevel(SystemLog::Level::INFO);

  perf.setup([&] {
    ASSERT_EQ(log.lastOpenStatus(), Status::OK);
    ASSERT_TRUE(log.isAsync()) << "ASYNC mode failed to start";
    for (int i = 0; i < 1000; ++i) {
      (void)log.info("WARM", "warmup", false);
    }
    log.flush();
  });

  auto result = perf.contentionRun([&] { (void)log.info("MT", MSG, false); }, "async-mt");

  std::printf("\nASYNC contended (%d threads): %.0f ops/s (%.1f us/op)\n", perf.threads(),
              result.callsPerSecond, result.stats.median);
}

/* ----------------------------- Skip Path ----------------------------- */

/**
 * @brief Below-threshold skip path latency (atomic load + branch).
 */
PERF_TEST(SkipPath, BelowThreshold) {
  UB_PERF_GUARD(perf);

  const auto PATH = ub::uniqTempFile("logs_skip");
  TempFileGuard cleanup{PATH};
  const std::string MSG = makeMsg(static_cast<std::size_t>(getCfg().msgBytes));

  SystemLog log(PATH.string(), SystemLog::Mode::ASYNC);
  log.setLevel(SystemLog::Level::ERROR);

  perf.setup([&] { ASSERT_EQ(log.lastOpenStatus(), Status::OK); });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)log.debug("PERF", MSG, 0);
    }
  });

  auto result =
      perf.throughputLoop([&] { ASSERT_EQ(log.debug("PERF", MSG, 0), Status::OK); }, "skip-path");

  std::printf("\nSkip path: %.0f ops/s (%.0f ns/op)\n", result.callsPerSecond,
              result.stats.median * 1000);
}

/* ----------------------------- Payload Sensitivity ----------------------------- */

/**
 * @brief Message size impact on formatting + I/O throughput.
 */
PERF_TEST(Payload, SizeSensitivity) {
  UB_PERF_GUARD(perf);

  const auto PATH = ub::uniqTempFile("logs_payload");
  TempFileGuard cleanup{PATH};
  const std::string MSG = makeMsg(static_cast<std::size_t>(getCfg().msgBytes));

  SystemLog log(PATH.string(), SystemLog::Mode::ASYNC, 8192);
  log.setLevel(SystemLog::Level::INFO);

  perf.setup([&] { ASSERT_EQ(log.lastOpenStatus(), Status::OK); });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)log.info("PERF", MSG, false);
    }
    log.flush();
  });

  auto result =
      perf.throughputLoop([&] { ASSERT_EQ(log.info("PERF", MSG, false), Status::OK); }, "payload");

  std::printf("\nPayload %d bytes: %.0f ops/s (%.1f us/op)\n", getCfg().msgBytes,
              result.callsPerSecond, result.stats.median);
}

PERF_MAIN()
