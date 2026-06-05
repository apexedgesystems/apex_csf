/**
 * @file SpiSocketDevice_pTest.cpp
 * @brief Performance tests for SPI over Unix socketpair.
 *
 * Measures:
 *  - Full-duplex transfer throughput at 64 B, 1 KB, and 4 KB
 *  - Write-only throughput at 64 B
 *  - Read-only throughput at 64 B
 *  - stats() access overhead
 *
 * Usage:
 *   ./SpiSocketDevice_PTEST --csv results.csv
 *   ./SpiSocketDevice_PTEST --quick
 *   ./SpiSocketDevice_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "SpiConfig.hpp"
#include "SpiSocketDevice.hpp"
#include "SpiStats.hpp"
#include "SpiStatus.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace ub = vernier::bench;
namespace sp = apex::protocols::spi;

/* ----------------------------- Local Helpers ----------------------------- */

namespace {

/**
 * @brief Create a socketpair and return both fds.
 */
bool createSocketPair(int& clientFd, int& peerFd) {
  int fds[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
    return false;
  }
  clientFd = fds[0];
  peerFd = fds[1];
  return true;
}

/**
 * @brief Echo thread: reads request [len:4][txData:len], writes back [rxData:len].
 *
 * Models the HW_MODEL side of the SpiSocketDevice wire protocol.
 */
void echoThread(int peerFd, std::atomic<bool>& done) {
  std::uint8_t buf[8192];
  while (!done.load(std::memory_order_relaxed)) {
    std::uint32_t len = 0;
    auto n = ::read(peerFd, &len, sizeof(len));
    if (n <= 0) {
      break;
    }
    if (n < static_cast<ssize_t>(sizeof(len))) {
      continue;
    }

    if (len > sizeof(buf)) {
      break;
    }

    std::size_t received = 0;
    while (received < len) {
      n = ::read(peerFd, buf + received, len - received);
      if (n <= 0) {
        break;
      }
      received += static_cast<std::size_t>(n);
    }
    if (received < len) {
      break;
    }

    std::size_t sent = 0;
    while (sent < len) {
      n = ::write(peerFd, buf + sent, len - sent);
      if (n <= 0) {
        break;
      }
      sent += static_cast<std::size_t>(n);
    }
  }
}

/**
 * @brief Helper to set up socketpair and SpiSocketDevice.
 */
struct TestContext {
  int clientFd = -1;
  int peerFd = -1;
  sp::SpiSocketDevice* device = nullptr;

  bool setup() {
    if (!createSocketPair(clientFd, peerFd)) {
      return false;
    }
    device = new sp::SpiSocketDevice(clientFd, true);
    sp::SpiConfig cfg;
    if (device->configure(cfg) != sp::Status::SUCCESS) {
      return false;
    }
    return true;
  }

  void teardown() {
    delete device;
    device = nullptr;
    clientFd = -1;
    if (peerFd >= 0) {
      ::close(peerFd);
      peerFd = -1;
    }
  }
};

} // namespace

/* ----------------------------- Transfer Throughput ----------------------------- */

/**
 * @brief Full-duplex transfer throughput for 64-byte payloads.
 */
PERF_TEST(SpiTransfer, Small64B) {
  UB_PERF_GUARD(perf);

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::uint8_t txData[64];
  std::uint8_t rxData[64];
  std::memset(txData, 0xAA, sizeof(txData));

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      (void)ctx.device->transfer(txData, rxData, sizeof(txData), 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto status = ctx.device->transfer(txData, rxData, sizeof(txData), 100);
        ASSERT_EQ(status, sp::Status::SUCCESS);
      },
      "transfer-64b");

  done.store(true, std::memory_order_relaxed);
  ::shutdown(ctx.peerFd, SHUT_RDWR);
  peer.join();
  ctx.peerFd = -1;
  ctx.teardown();

  std::printf("\nTransfer (64B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief Full-duplex transfer throughput for 1 KB payloads.
 */
PERF_TEST(SpiTransfer, Medium1KB) {
  UB_PERF_GUARD(perf);

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::vector<std::uint8_t> txData(1024, 0xBB);
  std::vector<std::uint8_t> rxData(1024, 0x00);

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      (void)ctx.device->transfer(txData.data(), rxData.data(), txData.size(), 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto status = ctx.device->transfer(txData.data(), rxData.data(), txData.size(), 100);
        ASSERT_EQ(status, sp::Status::SUCCESS);
      },
      "transfer-1kb");

  done.store(true, std::memory_order_relaxed);
  ::shutdown(ctx.peerFd, SHUT_RDWR);
  peer.join();
  ctx.peerFd = -1;
  ctx.teardown();

  std::printf("\nTransfer (1KB): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief Full-duplex transfer throughput for 4 KB payloads.
 */
PERF_TEST(SpiTransfer, Large4KB) {
  UB_PERF_GUARD(perf);

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::vector<std::uint8_t> txData(4096, 0xCC);
  std::vector<std::uint8_t> rxData(4096, 0x00);

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      (void)ctx.device->transfer(txData.data(), rxData.data(), txData.size(), 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto status = ctx.device->transfer(txData.data(), rxData.data(), txData.size(), 100);
        ASSERT_EQ(status, sp::Status::SUCCESS);
      },
      "transfer-4kb");

  done.store(true, std::memory_order_relaxed);
  ::shutdown(ctx.peerFd, SHUT_RDWR);
  peer.join();
  ctx.peerFd = -1;
  ctx.teardown();

  std::printf("\nTransfer (4KB): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- Write Throughput ----------------------------- */

/**
 * @brief Write-only throughput (no receive) for 64-byte payloads.
 */
PERF_TEST(SpiWrite, Only64B) {
  UB_PERF_GUARD(perf);

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::uint8_t txData[64];
  std::memset(txData, 0xDD, sizeof(txData));

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      (void)ctx.device->write(txData, sizeof(txData), 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto status = ctx.device->write(txData, sizeof(txData), 100);
        ASSERT_EQ(status, sp::Status::SUCCESS);
      },
      "write-64b");

  done.store(true, std::memory_order_relaxed);
  ::shutdown(ctx.peerFd, SHUT_RDWR);
  peer.join();
  ctx.peerFd = -1;
  ctx.teardown();

  std::printf("\nWrite (64B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- Read Latency ----------------------------- */

/**
 * @brief Read-only latency for 64-byte payloads.
 */
PERF_TEST(SpiRead, Only64B) {
  UB_PERF_GUARD(perf);

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::uint8_t rxData[64];

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      (void)ctx.device->read(rxData, sizeof(rxData), 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        auto status = ctx.device->read(rxData, sizeof(rxData), 100);
        ASSERT_EQ(status, sp::Status::SUCCESS);
      },
      "read-64b");

  done.store(true, std::memory_order_relaxed);
  ::shutdown(ctx.peerFd, SHUT_RDWR);
  peer.join();
  ctx.peerFd = -1;
  ctx.teardown();

  std::printf("\nRead (64B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- Overhead ----------------------------- */

/**
 * @brief Overhead of stats() call.
 */
PERF_TEST(SpiOverhead, Stats) {
  UB_PERF_GUARD(perf);

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  std::uint8_t data[4] = {0x01, 0x02, 0x03, 0x04};
  std::uint8_t rx[4] = {};
  (void)ctx.device->transfer(data, rx, sizeof(data), 100);

  done.store(true, std::memory_order_relaxed);
  ::shutdown(ctx.peerFd, SHUT_RDWR);
  peer.join();
  ctx.peerFd = -1;

  volatile std::uint64_t sink = 0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto& stats = ctx.device->stats();
      sink += stats.totalBytes();
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        const auto& stats = ctx.device->stats();
        sink += stats.totalBytes();
      },
      "stats-access");

  ctx.teardown();

  std::printf("\nstats() access: %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

PERF_MAIN()
