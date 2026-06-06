/**
 * @file UnixSocket_pTest.cpp
 * @brief Performance tests for Unix domain socket server/client echo operations.
 *
 * Measures:
 *  - Echo round-trip latency at 64 B, 1 KB, and 16 KB
 *  - Write-only throughput (fire-and-forget)
 *  - Multi-client scalability
 *  - Payload size scaling
 *
 * Usage:
 *   ./UnixSocket_PTEST --csv results.csv
 *   ./UnixSocket_PTEST --quick
 *   ./UnixSocket_PTEST --profile gperf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixSocketClient.hpp"
#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixSocketServer.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace ub = vernier::bench;
using namespace std::chrono_literals;

using apex::protocols::unix_socket::UNIX_CLIENT_SUCCESS;
using apex::protocols::unix_socket::UNIX_SERVER_SUCCESS;
using apex::protocols::unix_socket::UnixSocketClient;
using apex::protocols::unix_socket::UnixSocketServer;

/* ----------------------------- Test Fixtures ----------------------------- */

namespace {

std::string uniqueSocketPath(const char* testName) {
  return std::string("/tmp/apex_ptest_") + testName + "_" + std::to_string(getpid()) + ".sock";
}

/**
 * @brief RAII echo server for performance tests.
 *
 * Runs an event loop thread that echoes received data back to clients.
 */
class EchoServerFixture {
public:
  explicit EchoServerFixture(const std::string& path) : server_(path) {}

  bool start(std::string& error) {
    uint8_t status = server_.init(true, &error);
    if (status != UNIX_SERVER_SUCCESS)
      return false;

    server_.setOnClientReadable(apex::concurrency::Delegate<void, int>{echoCallback, this});

    running_.store(true, std::memory_order_relaxed);
    ioThread_ = std::thread([this]() {
      while (running_.load(std::memory_order_relaxed)) {
        server_.processEvents(50);
      }
    });

    std::this_thread::sleep_for(20ms);
    return true;
  }

  void stop() {
    running_.store(false, std::memory_order_relaxed);
    server_.stop();
    if (ioThread_.joinable())
      ioThread_.join();
  }

  ~EchoServerFixture() { stop(); }

  UnixSocketServer& server() { return server_; }

private:
  static void echoCallback(void* ctx, int clientfd) noexcept {
    auto* self = static_cast<EchoServerFixture*>(ctx);
    std::array<uint8_t, 4096> buf{};
    ssize_t nread = self->server_.read(clientfd, buf, nullptr);
    if (nread > 0) {
      apex::compat::bytes_span out(buf.data(), static_cast<size_t>(nread));
      (void)self->server_.write(clientfd, out, nullptr);
    }
  }

  UnixSocketServer server_;
  std::thread ioThread_;
  std::atomic_bool running_{false};
};

std::vector<uint8_t> generatePayload(size_t size) {
  std::vector<uint8_t> payload(size);
  std::iota(payload.begin(), payload.end(), static_cast<uint8_t>(1));
  return payload;
}

ssize_t syncRead(UnixSocketClient& client, apex::compat::bytes_span buf, int timeoutMs) {
  if (!client.waitReadable(timeoutMs))
    return -1;
  return client.read(buf, nullptr);
}

} // namespace

/* ----------------------------- Echo Latency ----------------------------- */

/**
 * @brief Unix socket echo round-trip latency with small (64 B) payload.
 */
PERF_TEST(UnixSocketPerf, EchoLatencySmall) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr size_t PAYLOAD_SIZE = 64;
  const std::string SOCK_PATH = uniqueSocketPath("echo_small");

  EchoServerFixture server(SOCK_PATH);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  UnixSocketClient client(SOCK_PATH);
  std::string cliErr;
  ASSERT_EQ(client.init(&cliErr), UNIX_CLIENT_SUCCESS) << cliErr;

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::array<uint8_t, 128> echoBuf{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
      (void)syncRead(client, echoBuf, 1000);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        ssize_t nw =
            client.write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
        ASSERT_GT(nw, 0) << "Write failed";
        ssize_t nr = syncRead(client, echoBuf, 1000);
        ASSERT_GT(nr, 0) << "Read failed";
      },
      "echo_64B");

  std::printf("\n[EchoLatencySmall] %zu bytes\n", PAYLOAD_SIZE);
  std::printf("  Round-trip: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f echo/sec\n", result.callsPerSecond);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);

  unlink(SOCK_PATH.c_str());
}

/**
 * @brief Unix socket echo round-trip latency with medium (1 KB) payload.
 */
PERF_TEST(UnixSocketPerf, EchoLatencyMedium) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr size_t PAYLOAD_SIZE = 1024;
  const std::string SOCK_PATH = uniqueSocketPath("echo_medium");

  EchoServerFixture server(SOCK_PATH);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  UnixSocketClient client(SOCK_PATH);
  std::string cliErr;
  ASSERT_EQ(client.init(&cliErr), UNIX_CLIENT_SUCCESS) << cliErr;

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::vector<uint8_t> echoBuf(PAYLOAD_SIZE * 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
      (void)syncRead(client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()),
                     1000);
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = PAYLOAD_SIZE,
      .bytesWritten = PAYLOAD_SIZE,
      .bytesAllocated = 0,
  };

  auto result = perf.throughputLoop(
      [&] {
        ssize_t nw =
            client.write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
        ASSERT_GT(nw, 0) << "Write failed";
        ssize_t nr = syncRead(
            client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), 1000);
        ASSERT_GT(nr, 0) << "Read failed";
      },
      "echo_1KB", memProfile);

  std::printf("\n[EchoLatencyMedium] %zu bytes\n", PAYLOAD_SIZE);
  std::printf("  Round-trip: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f echo/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE * 2) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);

  unlink(SOCK_PATH.c_str());
}

/**
 * @brief Unix socket echo round-trip latency with large (16 KB) payload.
 */
PERF_TEST(UnixSocketPerf, EchoLatencyLarge) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr size_t PAYLOAD_SIZE = static_cast<const size_t>(16 * 1024);
  const std::string SOCK_PATH = uniqueSocketPath("echo_large");

  EchoServerFixture server(SOCK_PATH);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  UnixSocketClient client(SOCK_PATH);
  std::string cliErr;
  ASSERT_EQ(client.init(&cliErr), UNIX_CLIENT_SUCCESS) << cliErr;

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::vector<uint8_t> echoBuf(PAYLOAD_SIZE * 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
      size_t totalRead = 0;
      while (totalRead < PAYLOAD_SIZE) {
        if (!client.waitReadable(1000))
          break;
        ssize_t nr = client.read(apex::compat::mutable_bytes_span(echoBuf.data() + totalRead,
                                                                  echoBuf.size() - totalRead),
                                 nullptr);
        if (nr <= 0)
          break;
        totalRead += static_cast<size_t>(nr);
      }
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = PAYLOAD_SIZE,
      .bytesWritten = PAYLOAD_SIZE,
      .bytesAllocated = 0,
  };

  auto result = perf.throughputLoop(
      [&] {
        ssize_t nw =
            client.write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
        ASSERT_GT(nw, 0) << "Write failed";

        size_t totalRead = 0;
        while (totalRead < PAYLOAD_SIZE) {
          if (!client.waitReadable(1000))
            break;
          ssize_t nr = client.read(apex::compat::mutable_bytes_span(echoBuf.data() + totalRead,
                                                                    echoBuf.size() - totalRead),
                                   nullptr);
          if (nr <= 0)
            break;
          totalRead += static_cast<size_t>(nr);
        }
        ASSERT_GE(totalRead, PAYLOAD_SIZE) << "Incomplete echo";
      },
      "echo_16KB", memProfile);

  std::printf("\n[EchoLatencyLarge] %zu bytes\n", PAYLOAD_SIZE);
  std::printf("  Round-trip: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f echo/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE * 2) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);

  unlink(SOCK_PATH.c_str());
}

/* ----------------------------- Throughput ----------------------------- */

/**
 * @brief RT-safe drain callback context for throughput test.
 */
struct UnixDrainContext {
  UnixSocketServer* server;
};

void unixDrainCallback(void* ctx, int clientfd) noexcept {
  auto* drainctx = static_cast<UnixDrainContext*>(ctx);
  std::array<uint8_t, 8192> buf{};
  while (drainctx->server->read(clientfd, buf, nullptr) > 0) {
  }
}

/**
 * @brief Maximum write throughput (fire-and-forget pattern).
 */
PERF_TEST(UnixSocketPerf, WriteThroughput) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr size_t PAYLOAD_SIZE = 1024;
  const std::string SOCK_PATH = uniqueSocketPath("write_tp");

  UnixSocketServer server(SOCK_PATH);
  std::string srvErr;
  ASSERT_EQ(server.init(true, &srvErr), UNIX_SERVER_SUCCESS) << srvErr;

  UnixDrainContext drainCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{unixDrainCallback, &drainCtx});

  std::atomic_bool running{true};
  std::thread ioThread([&]() {
    while (running.load(std::memory_order_relaxed)) {
      server.processEvents(50);
    }
  });
  std::this_thread::sleep_for(20ms);

  UnixSocketClient client(SOCK_PATH);
  std::string cliErr;
  ASSERT_EQ(client.init(&cliErr), UNIX_CLIENT_SUCCESS) << cliErr;

  auto payload = generatePayload(PAYLOAD_SIZE);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = 0,
      .bytesWritten = PAYLOAD_SIZE,
      .bytesAllocated = 0,
  };

  volatile ssize_t sink = 0;
  auto result = perf.throughputLoop(
      [&] {
        sink = client.write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
      },
      "write_1KB", memProfile);

  std::printf("\n[WriteThroughput] %zu bytes per write\n", PAYLOAD_SIZE);
  std::printf("  Latency: %.3f us (median)\n", result.stats.median);
  std::printf("  Throughput: %.0f writes/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);

  running = false;
  server.stop();
  ioThread.join();

  (void)sink;
  unlink(SOCK_PATH.c_str());
}

/* ----------------------------- Multi-Client ----------------------------- */

/**
 * @brief Server performance under concurrent client load.
 */
PERF_TEST(UnixSocketPerf, MultiClientEcho) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr size_t PAYLOAD_SIZE = 256;
  constexpr size_t NUM_CLIENTS = 4;
  const std::string SOCK_PATH = uniqueSocketPath("multi");

  EchoServerFixture server(SOCK_PATH);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  std::vector<std::unique_ptr<UnixSocketClient>> clients;
  for (size_t i = 0; i < NUM_CLIENTS; ++i) {
    auto client = std::make_unique<UnixSocketClient>(SOCK_PATH);
    std::string cliErr;
    ASSERT_EQ(client->init(&cliErr), UNIX_CLIENT_SUCCESS) << "Client " << i << ": " << cliErr;
    clients.push_back(std::move(client));
  }

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::vector<uint8_t> echoBuf(PAYLOAD_SIZE * 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (auto& client : clients) {
        (void)client->write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
        (void)syncRead(*client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()),
                       1000);
      }
    }
  });

  size_t clientIdx = 0;
  auto result = perf.throughputLoop(
      [&] {
        auto& client = clients[clientIdx];
        clientIdx = (clientIdx + 1) % NUM_CLIENTS;

        ssize_t nw =
            client->write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
        ASSERT_GT(nw, 0) << "Write failed";
        ssize_t nr = syncRead(
            *client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), 1000);
        ASSERT_GT(nr, 0) << "Read failed";
      },
      "multi_client");

  std::printf("\n[MultiClientEcho] %zu clients, %zu bytes\n", NUM_CLIENTS, PAYLOAD_SIZE);
  std::printf("  Round-trip: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Aggregate throughput: %.0f echo/sec\n", result.callsPerSecond);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);

  unlink(SOCK_PATH.c_str());
}

/* ----------------------------- Payload Scaling ----------------------------- */

/**
 * @brief Latency comparison across payload sizes.
 */
PERF_TEST(UnixSocketPerf, PayloadScaling) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  const std::string SOCK_PATH = uniqueSocketPath("scaling");

  EchoServerFixture server(SOCK_PATH);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  UnixSocketClient client(SOCK_PATH);
  std::string cliErr;
  ASSERT_EQ(client.init(&cliErr), UNIX_CLIENT_SUCCESS) << cliErr;

  struct TestCase {
    const char* name;
    size_t size;
  };

  std::vector<TestCase> tests = {{"32B", 32},   {"64B", 64},     {"128B", 128},  {"256B", 256},
                                 {"512B", 512}, {"1KB", 1024},   {"2KB", 2048},  {"4KB", 4096},
                                 {"8KB", 8192}, {"16KB", 16384}, {"32KB", 32768}};

  std::printf("\n%-8s %-12s %-15s %-12s\n", "Size", "Latency(us)", "Throughput", "MB/s");
  std::printf("%s\n", std::string(50, '-').c_str());

  for (const auto& test : tests) {
    auto payload = generatePayload(test.size);
    std::vector<uint8_t> echoBuf(test.size * 2);

    for (int i = 0; i < 100; ++i) {
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
      size_t total = 0;
      while (total < test.size) {
        if (!client.waitReadable(1000))
          break;
        ssize_t nr = client.read(
            apex::compat::mutable_bytes_span(echoBuf.data() + total, echoBuf.size() - total),
            nullptr);
        if (nr <= 0)
          break;
        total += static_cast<size_t>(nr);
      }
    }

    ub::PerfConfig cfg = ub::detail::getPerfConfig();
    cfg.msgBytes = static_cast<int>(test.size);
    std::string testName = "UnixSocketPerf.PayloadScaling/" + std::string(test.name);
    ub::PerfCase subPerf{testName, cfg};

    auto result = subPerf.throughputLoop(
        [&] {
          (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), nullptr);
          size_t total = 0;
          while (total < test.size) {
            if (!client.waitReadable(1000))
              break;
            ssize_t nr = client.read(
                apex::compat::mutable_bytes_span(echoBuf.data() + total, echoBuf.size() - total),
                nullptr);
            if (nr <= 0)
              break;
            total += static_cast<size_t>(nr);
          }
        },
        test.name);

    double mbPerSec = (result.callsPerSecond * test.size * 2) / 1e6;
    std::printf("%-8s %-12.1f %-15.0f %-12.1f\n", test.name, result.stats.median,
                result.callsPerSecond, mbPerSec);
  }

  unlink(SOCK_PATH.c_str());
}

PERF_MAIN()
