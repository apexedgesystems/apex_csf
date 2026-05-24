/**
 * @file I2cSocketDevice_pTest.cpp
 * @brief Performance tests for I2C over Unix socketpair.
 *
 * Measures:
 *  - Write throughput at 8 B and 64 B
 *  - Read throughput at 8 B and 64 B
 *  - Combined writeRead register-access transaction throughput
 *  - stats() access overhead
 *
 * Usage:
 *   ./I2cSocketDevice_PTEST --csv results.csv
 *   ./I2cSocketDevice_PTEST --quick
 *   ./I2cSocketDevice_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "I2cConfig.hpp"
#include "I2cSocketDevice.hpp"
#include "I2cStats.hpp"
#include "I2cStatus.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace ub = vernier::bench;
namespace ic = apex::protocols::i2c;

/* ----------------------------- Local Helpers ----------------------------- */

namespace {

inline const ub::PerfConfig& getCfg() { return ub::detail::getPerfConfig(); }

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
 * @brief Echo thread implementing the I2C wire protocol peer.
 *
 * Wire protocol:
 *   Request:  [addr:2 LE][wLen:2 LE][rLen:2 LE][wData:wLen]
 *   Response: [status:1][rData:rLen]
 *
 * Status byte: 0 = ACK (success). Echoes write data as read data
 * (truncated or zero-padded to rLen).
 */
void echoThread(int peerFd, std::atomic<bool>& done) {
  std::uint8_t buf[8192];
  while (!done.load(std::memory_order_relaxed)) {
    std::uint8_t hdr[6];
    auto n = ::read(peerFd, hdr, sizeof(hdr));
    if (n <= 0) {
      break;
    }
    if (n < static_cast<ssize_t>(sizeof(hdr))) {
      continue;
    }

    std::uint16_t wLen = 0;
    std::uint16_t rLen = 0;
    std::memcpy(&wLen, &hdr[2], 2);
    std::memcpy(&rLen, &hdr[4], 2);

    if (wLen > 0) {
      if (wLen > sizeof(buf)) {
        break;
      }
      std::size_t received = 0;
      while (received < wLen) {
        n = ::read(peerFd, buf + received, wLen - received);
        if (n <= 0) {
          break;
        }
        received += static_cast<std::size_t>(n);
      }
      if (received < wLen) {
        break;
      }
    }

    std::uint8_t status = 0;
    if (::write(peerFd, &status, 1) <= 0) {
      break;
    }

    if (rLen > 0) {
      std::uint8_t rBuf[8192];
      std::memset(rBuf, 0, rLen);
      if (wLen > 0) {
        std::size_t copyLen = (wLen < rLen) ? wLen : rLen;
        std::memcpy(rBuf, buf, copyLen);
      }
      std::size_t sent = 0;
      while (sent < rLen) {
        n = ::write(peerFd, rBuf + sent, rLen - sent);
        if (n <= 0) {
          break;
        }
        sent += static_cast<std::size_t>(n);
      }
    }
  }
}

/**
 * @brief Helper to set up socketpair and I2cSocketDevice.
 */
struct TestContext {
  int clientFd = -1;
  int peerFd = -1;
  ic::I2cSocketDevice* device = nullptr;

  bool setup() {
    if (!createSocketPair(clientFd, peerFd)) {
      return false;
    }
    device = new ic::I2cSocketDevice(clientFd, true);
    ic::I2cConfig cfg;
    if (device->configure(cfg) != ic::Status::SUCCESS) {
      return false;
    }
    if (device->setSlaveAddress(0x50) != ic::Status::SUCCESS) {
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

/* ----------------------------- Write Throughput ----------------------------- */

/**
 * @brief Write throughput for 8-byte payloads (typical register write).
 */
PERF_TEST(I2cWrite, Small8B) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::uint8_t txData[8];
  std::memset(txData, 0xAA, sizeof(txData));

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t written = 0;
      (void)ctx.device->write(txData, sizeof(txData), written, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto status = ctx.device->write(txData, sizeof(txData), written, 100);
        ASSERT_EQ(status, ic::Status::SUCCESS);
      },
      "write-8b");

  done.store(true, std::memory_order_relaxed);
  ::shutdown(ctx.peerFd, SHUT_RDWR);
  peer.join();
  ctx.peerFd = -1;
  ctx.teardown();

  std::printf("\nWrite (8B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief Write throughput for 64-byte payloads.
 */
PERF_TEST(I2cWrite, Medium64B) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::uint8_t txData[64];
  std::memset(txData, 0xBB, sizeof(txData));

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t written = 0;
      (void)ctx.device->write(txData, sizeof(txData), written, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t written = 0;
        auto status = ctx.device->write(txData, sizeof(txData), written, 100);
        ASSERT_EQ(status, ic::Status::SUCCESS);
      },
      "write-64b");

  done.store(true, std::memory_order_relaxed);
  ::shutdown(ctx.peerFd, SHUT_RDWR);
  peer.join();
  ctx.peerFd = -1;
  ctx.teardown();

  std::printf("\nWrite (64B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- Read Throughput ----------------------------- */

/**
 * @brief Read throughput for 8-byte payloads (typical register read).
 */
PERF_TEST(I2cRead, Small8B) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::uint8_t rxData[8];

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t bytesRead = 0;
      (void)ctx.device->read(rxData, sizeof(rxData), bytesRead, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t bytesRead = 0;
        auto status = ctx.device->read(rxData, sizeof(rxData), bytesRead, 100);
        ASSERT_EQ(status, ic::Status::SUCCESS);
      },
      "read-8b");

  done.store(true, std::memory_order_relaxed);
  ::shutdown(ctx.peerFd, SHUT_RDWR);
  peer.join();
  ctx.peerFd = -1;
  ctx.teardown();

  std::printf("\nRead (8B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/**
 * @brief Read throughput for 64-byte payloads.
 */
PERF_TEST(I2cRead, Medium64B) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::uint8_t rxData[64];

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t bytesRead = 0;
      (void)ctx.device->read(rxData, sizeof(rxData), bytesRead, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t bytesRead = 0;
        auto status = ctx.device->read(rxData, sizeof(rxData), bytesRead, 100);
        ASSERT_EQ(status, ic::Status::SUCCESS);
      },
      "read-64b");

  done.store(true, std::memory_order_relaxed);
  ::shutdown(ctx.peerFd, SHUT_RDWR);
  peer.join();
  ctx.peerFd = -1;
  ctx.teardown();

  std::printf("\nRead (64B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- WriteRead Throughput ----------------------------- */

/**
 * @brief Combined write-read transaction throughput (register read pattern).
 */
PERF_TEST(I2cWriteRead, Register) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::uint8_t regAddr = 0x00;
  std::uint8_t rxData[8];

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  perf.warmup([&] {
    for (int i = 0; i < std::min(perf.cycles(), 100); ++i) {
      std::size_t bytesRead = 0;
      (void)ctx.device->writeRead(&regAddr, 1, rxData, sizeof(rxData), bytesRead, 100);
    }
  });

  const ub::PerfResult R = perf.throughputLoop(
      [&] {
        std::size_t bytesRead = 0;
        auto status = ctx.device->writeRead(&regAddr, 1, rxData, sizeof(rxData), bytesRead, 100);
        ASSERT_EQ(status, ic::Status::SUCCESS);
      },
      "writeread-reg");

  done.store(true, std::memory_order_relaxed);
  ::shutdown(ctx.peerFd, SHUT_RDWR);
  peer.join();
  ctx.peerFd = -1;
  ctx.teardown();

  std::printf("\nWriteRead (1+8B): %.3f us (%.0f ops/s)\n", R.stats.median, R.callsPerSecond);
}

/* ----------------------------- Overhead ----------------------------- */

/**
 * @brief Overhead of stats() call.
 */
PERF_TEST(I2cOverhead, Stats) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, getCfg());

  TestContext ctx;
  ASSERT_TRUE(ctx.setup());

  std::atomic<bool> done{false};
  std::thread peer(echoThread, ctx.peerFd, std::ref(done));

  std::uint8_t data[4] = {0x01, 0x02, 0x03, 0x04};
  std::size_t written = 0;
  (void)ctx.device->write(data, sizeof(data), written, 100);

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
