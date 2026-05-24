/**
 * @file RfcommAdapter_pTest.cpp
 * @brief Performance tests for the Bluetooth RFCOMM adapter via loopback.
 *
 * Measures:
 *  - Write throughput at 64 B and 4 KB
 *  - Read latency for 32 B payloads
 *  - Round-trip echo latency
 *  - stats() access overhead
 *  - ByteTrace overhead when tracing is enabled
 *
 * Usage:
 *   ./RfcommAdapter_PTEST --csv results.csv
 *   ./RfcommAdapter_PTEST --quick
 *   ./RfcommAdapter_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "RfcommAdapter.hpp"
#include "RfcommLoopback.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace ub = vernier::bench;
namespace bt = apex::protocols::wireless::bluetooth;
using apex::protocols::TraceDirection;

/* ----------------------------- Local Helpers ----------------------------- */

namespace {

inline const ub::PerfConfig& getCfg() { return ub::detail::getPerfConfig(); }

/**
 * @brief Helper to set up loopback and adapter for tests.
 */
struct TestContext {
  bt::RfcommLoopback loopback;
  std::unique_ptr<bt::RfcommAdapter> adapter;

  bool setup() {
    if (loopback.open() != bt::Status::SUCCESS) {
      return false;
    }
    int fd = loopback.releaseClientFd();
    if (fd < 0) {
      return false;
    }
    adapter = std::make_unique<bt::RfcommAdapter>(fd);
    return true;
  }

  void teardown() {
    adapter.reset();
    loopback.close();
  }
};

} // namespace

/* ----------------------------- Write Throughput ----------------------------- */

/**
 * @brief Write throughput for small (64 B) buffers.
 */
PERF_TEST(RfcommWrite, Small64B) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::uint8_t data[64];
  std::memset(data, 0xAA, sizeof(data));

  std::atomic<bool> done{false};
  std::thread drainer([&ctx, &done]() {
    std::uint8_t buf[4096];
    while (!done.load(std::memory_order_relaxed)) {
      std::size_t bytesRead = 0;
      (void)ctx.loopback.serverRead({buf, sizeof(buf)}, bytesRead, 10);
    }
  });

  std::size_t totalWritten = 0;

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t written = 0;
      (void)ctx.adapter->write({data, sizeof(data)}, written, 100);
      totalWritten += written;
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto status = ctx.adapter->write({data, sizeof(data)}, written, 100);
        ASSERT_TRUE(status == bt::Status::SUCCESS || status == bt::Status::WOULD_BLOCK);
        totalWritten += written;
      },
      "write-64b");

  done.store(true, std::memory_order_relaxed);
  drainer.join();
  ctx.teardown();

  std::printf("\nWrite (64B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief Write throughput for large (4 KB) buffers.
 */
PERF_TEST(RfcommWrite, Large4KB) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::vector<std::uint8_t> data(4096, 0xBB);

  std::atomic<bool> done{false};
  std::thread drainer([&ctx, &done]() {
    std::uint8_t buf[8192];
    while (!done.load(std::memory_order_relaxed)) {
      std::size_t bytesRead = 0;
      (void)ctx.loopback.serverRead({buf, sizeof(buf)}, bytesRead, 10);
    }
  });

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t written = 0;
      (void)ctx.adapter->write({data.data(), data.size()}, written, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto status = ctx.adapter->write({data.data(), data.size()}, written, 100);
        ASSERT_TRUE(status == bt::Status::SUCCESS || status == bt::Status::WOULD_BLOCK);
      },
      "write-4kb");

  done.store(true, std::memory_order_relaxed);
  drainer.join();
  ctx.teardown();

  std::printf("\nWrite (4KB): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- Read Latency ----------------------------- */

/**
 * @brief Read latency when data is available.
 */
PERF_TEST(RfcommRead, Latency) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::uint8_t writeData[32];
  std::memset(writeData, 0xCC, sizeof(writeData));
  std::uint8_t readBuf[64];

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t written = 0;
      (void)ctx.loopback.serverWrite({writeData, sizeof(writeData)}, written, 100);

      std::size_t bytesRead = 0;
      (void)ctx.adapter->read({readBuf, sizeof(readBuf)}, bytesRead, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        (void)ctx.loopback.serverWrite({writeData, sizeof(writeData)}, written, 100);

        std::size_t bytesRead = 0;
        auto status = ctx.adapter->read({readBuf, sizeof(readBuf)}, bytesRead, 100);
        ASSERT_EQ(status, bt::Status::SUCCESS);
      },
      "read-latency");

  ctx.teardown();

  std::printf("\nRead Latency (32B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- Round-Trip ----------------------------- */

/**
 * @brief Write-read round-trip latency with echo.
 */
PERF_TEST(RfcommRoundTrip, Echo) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::uint8_t writeData[16];
  std::memset(writeData, 0xDD, sizeof(writeData));
  std::uint8_t readBuf[64];

  std::atomic<bool> done{false};
  std::thread echoer([&ctx, &done]() {
    std::uint8_t buf[256];
    while (!done.load(std::memory_order_relaxed)) {
      std::size_t bytesRead = 0;
      auto status = ctx.loopback.serverRead({buf, sizeof(buf)}, bytesRead, 10);
      if (status == bt::Status::SUCCESS && bytesRead > 0) {
        std::size_t written = 0;
        (void)ctx.loopback.serverWrite({buf, bytesRead}, written, 10);
      }
    }
  });

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t written = 0;
      (void)ctx.adapter->write({writeData, sizeof(writeData)}, written, 100);

      std::size_t bytesRead = 0;
      (void)ctx.adapter->read({readBuf, sizeof(readBuf)}, bytesRead, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto wStatus = ctx.adapter->write({writeData, sizeof(writeData)}, written, 100);
        ASSERT_EQ(wStatus, bt::Status::SUCCESS);

        std::size_t bytesRead = 0;
        auto rStatus = ctx.adapter->read({readBuf, sizeof(readBuf)}, bytesRead, 100);
        ASSERT_EQ(rStatus, bt::Status::SUCCESS);
      },
      "round-trip-echo");

  done.store(true, std::memory_order_relaxed);
  echoer.join();
  ctx.teardown();

  std::printf("\nRound-trip Echo (16B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- Overhead ----------------------------- */

/**
 * @brief Overhead of stats() call.
 */
PERF_TEST(RfcommOverhead, Stats) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  const std::uint8_t data[] = {0x01};
  std::size_t written = 0;
  (void)ctx.adapter->write({data, sizeof(data)}, written, 100);

  volatile std::uint64_t sink = 0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto& stats = ctx.adapter->stats();
      sink += stats.totalBytes();
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        const auto& stats = ctx.adapter->stats();
        sink += stats.totalBytes();
      },
      "stats-access");

  ctx.teardown();

  std::printf("\nstats() access: %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief ByteTrace overhead when enabled.
 */
PERF_TEST(RfcommOverhead, ByteTrace) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  auto nullCallback = [](TraceDirection, const std::uint8_t*, std::size_t, void*) noexcept {};
  ctx.adapter->attachTrace(nullCallback);
  ctx.adapter->setTraceEnabled(true);

  std::uint8_t data[64];
  std::memset(data, 0xEE, sizeof(data));

  std::atomic<bool> done{false};
  std::thread drainer([&ctx, &done]() {
    std::uint8_t buf[4096];
    while (!done.load(std::memory_order_relaxed)) {
      std::size_t bytesRead = 0;
      (void)ctx.loopback.serverRead({buf, sizeof(buf)}, bytesRead, 10);
    }
  });

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t written = 0;
      (void)ctx.adapter->write({data, sizeof(data)}, written, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto status = ctx.adapter->write({data, sizeof(data)}, written, 100);
        ASSERT_TRUE(status == bt::Status::SUCCESS || status == bt::Status::WOULD_BLOCK);
      },
      "write-with-trace");

  done.store(true, std::memory_order_relaxed);
  drainer.join();
  ctx.teardown();

  std::printf("\nWrite with ByteTrace (64B): %.3f us (%.0f ops/s)\n", R.stats.median,
              R.callsPerSecond);
}

PERF_MAIN()
