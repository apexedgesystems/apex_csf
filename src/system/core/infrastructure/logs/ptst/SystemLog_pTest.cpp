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

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
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

/* ----------------------------- Sporadic Pattern ----------------------------- */

/**
 * @brief Sporadic log cycle: one entry on an always-empty queue, then wait
 *        for the I/O thread to drain it.
 *
 * The dominant production pattern -- components log occasionally, so nearly
 * every call takes the empty-to-non-empty wakeup path (the one the
 * saturated-queue tests never exercise). The measured cycle includes the
 * enqueue, the wakeup, and the drain wait; it is the A/B instrument for
 * wakeup-path changes.
 */
PERF_TEST(SporadicPattern, SingleLogDrainCycle) {
  UB_PERF_GUARD(perf);

  const auto PATH = ub::uniqTempFile("logs_sporadic");
  TempFileGuard cleanup{PATH};

  SystemLog log(PATH.string(), SystemLog::Mode::ASYNC);
  log.setLevel(SystemLog::Level::INFO);
  auto backend = log.asyncBackend();

  perf.setup([&] {
    ASSERT_EQ(log.lastOpenStatus(), Status::OK);
    ASSERT_TRUE(log.isAsync());
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)log.info("SPOR", "sporadic warmup", false);
      while (backend->queueDepth() > 0) {
      }
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        (void)log.info("SPOR", "sporadic entry", false);
        while (backend->queueDepth() > 0) {
        }
      },
      "sporadic-cycle");

  std::printf("\nSporadic log+drain cycle: %.0f cycles/s (%.1f us/cycle)\n", result.callsPerSecond,
              result.stats.median);
}

/* ----------------------------- Overflow Behavior ----------------------------- */

/**
 * @brief Burst past a tiny queue: drop path cost and counter accounting.
 *
 * A 16-entry queue saturates immediately under a burst, so most calls take
 * the queue-full drop path. Proves drops are counted and measures the cost
 * a component pays when the backend cannot keep up.
 */
PERF_TEST(OverflowBehavior, BurstIntoTinyQueue) {
  UB_PERF_GUARD(perf);

  const auto PATH = ub::uniqTempFile("logs_overflow");
  TempFileGuard cleanup{PATH};
  const std::string MSG = makeMsg(256);

  SystemLog log(PATH.string(), SystemLog::Mode::ASYNC, 16);
  log.setLevel(SystemLog::Level::INFO);
  auto backend = log.asyncBackend();

  perf.setup([&] {
    ASSERT_EQ(log.lastOpenStatus(), Status::OK);
    ASSERT_TRUE(log.isAsync());
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)log.info("OVFL", MSG, false);
    }
  });

  const std::uint64_t DROPS_BEFORE = backend->droppedCount();
  auto result = perf.throughputLoop([&] { (void)log.info("OVFL", MSG, false); }, "burst-overflow");
  const std::uint64_t DROPS_AFTER = backend->droppedCount();

  ASSERT_GT(DROPS_AFTER, DROPS_BEFORE) << "burst never overflowed the 16-entry queue";
  std::printf("\nOverflow burst: %.0f ops/s (%.1f us/op), %llu drops counted\n",
              result.callsPerSecond, result.stats.median,
              static_cast<unsigned long long>(DROPS_AFTER - DROPS_BEFORE));
}

/* ----------------------------- Flush Latency ----------------------------- */

/**
 * @brief flush() latency after a 256-entry burst.
 *
 * Measures the control-plane drain cost a caller pays at shutdown or at a
 * checkpoint: enqueue a burst, then block in flush() until the queue is
 * empty and the file is synced.
 */
PERF_TEST(FlushLatency, BurstThenFlush) {
  UB_PERF_GUARD(perf);

  const auto PATH = ub::uniqTempFile("logs_flush");
  TempFileGuard cleanup{PATH};
  const std::string MSG = makeMsg(static_cast<std::size_t>(getCfg().msgBytes));

  SystemLog log(PATH.string(), SystemLog::Mode::ASYNC);
  log.setLevel(SystemLog::Level::INFO);

  perf.setup([&] {
    ASSERT_EQ(log.lastOpenStatus(), Status::OK);
    ASSERT_TRUE(log.isAsync());
  });

  perf.warmup([&] {
    for (int i = 0; i < 256; ++i) {
      (void)log.info("FLSH", MSG, false);
    }
    (void)log.flush();
  });

  auto result = perf.throughputLoop(
      [&] {
        for (int i = 0; i < 256; ++i) {
          (void)log.info("FLSH", MSG, false);
        }
        ASSERT_EQ(log.flush(), Status::OK);
      },
      "burst-flush");

  std::printf("\nBurst(256)+flush: %.0f cycles/s (%.1f us/cycle)\n", result.callsPerSecond,
              result.stats.median);
}

/* ----------------------------- Fleet Cost ----------------------------- */

/**
 * @brief Sporadic logging across a 16-backend fleet.
 *
 * The deployed shape gives every component its own ASYNC backend (own I/O
 * thread, own queue). Round-robin sporadic logging across 16 of them makes
 * every call wake a different sleeping I/O thread -- the fleet-scale cost
 * of the per-component architecture, and the baseline for the shared-drain
 * design decision.
 */
PERF_TEST(FleetCost, SixteenBackendsSporadic) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t FLEET = 16;
  std::array<std::unique_ptr<SystemLog>, FLEET> fleet;
  std::array<TempFileGuard, FLEET> cleanups;
  for (std::size_t i = 0; i < FLEET; ++i) {
    const auto PATH = ub::uniqTempFile("logs_fleet");
    cleanups[i].p = PATH;
    fleet[i] = std::make_unique<SystemLog>(PATH.string(), SystemLog::Mode::ASYNC);
    fleet[i]->setLevel(SystemLog::Level::INFO);
  }

  perf.setup([&] {
    for (auto& log : fleet) {
      ASSERT_EQ(log->lastOpenStatus(), Status::OK);
      ASSERT_TRUE(log->isAsync());
    }
  });

  std::size_t idx = 0;
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto& log = *fleet[idx++ % FLEET];
      (void)log.info("FLEET", "fleet entry", false);
      while (log.asyncBackend()->queueDepth() > 0) {
      }
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        auto& log = *fleet[idx++ % FLEET];
        (void)log.info("FLEET", "fleet entry", false);
        while (log.asyncBackend()->queueDepth() > 0) {
        }
      },
      "fleet-sporadic");

  std::printf("\nFleet(16) sporadic cycle: %.0f cycles/s (%.1f us/cycle)\n", result.callsPerSecond,
              result.stats.median);
}

PERF_MAIN()
