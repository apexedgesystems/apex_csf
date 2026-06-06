/**
 * @file TcpSocket_pTest.cpp
 * @brief Performance tests for TCP socket server/client echo operations.
 *
 * Measures:
 *  - Echo round-trip latency at 64 B, 1 KB, and 16 KB
 *  - Write-only throughput (fire-and-forget)
 *  - Multi-client scalability
 *  - Payload size scaling
 *
 * Usage:
 *   ./TcpSocket_PTEST --csv results.csv
 *   ./TcpSocket_PTEST --quick
 *   ./TcpSocket_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketClient.hpp"
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketServer.hpp"
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
#include <vector>

namespace ub = vernier::bench;
using namespace std::chrono_literals;

using apex::protocols::tcp::TCP_CLIENT_SUCCESS;
using apex::protocols::tcp::TCP_SERVER_SUCCESS;
using apex::protocols::tcp::TcpSocketClient;
using apex::protocols::tcp::TcpSocketServer;

/* ----------------------------- Test Fixtures ----------------------------- */

namespace {

/**
 * @brief RAII echo server for performance tests.
 *
 * Runs an event loop thread that echoes received data back to clients.
 */
class EchoServerFixture {
public:
  EchoServerFixture(const std::string& ip, const std::string& port) : server_(ip, port) {}

  bool start(std::string& error) {
    uint8_t status = server_.init(error);
    if (status != TCP_SERVER_SUCCESS)
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

  TcpSocketServer& server() { return server_; }

private:
  static void echoCallback(void* ctx, int clientfd) noexcept {
    auto* self = static_cast<EchoServerFixture*>(ctx);
    std::array<uint8_t, 4096> buf{};
    ssize_t nread = self->server_.read(clientfd, buf, 0);
    if (nread > 0) {
      apex::compat::bytes_span out(buf.data(), static_cast<size_t>(nread));
      (void)self->server_.write(clientfd, out);
    }
  }

  TcpSocketServer server_;
  std::thread ioThread_;
  std::atomic_bool running_{false};
};

std::vector<uint8_t> generatePayload(size_t size) {
  std::vector<uint8_t> payload(size);
  std::iota(payload.begin(), payload.end(), static_cast<uint8_t>(1));
  return payload;
}

} // namespace

/* ----------------------------- Echo Latency ----------------------------- */

/**
 * @brief TCP echo round-trip latency with small (64 B) payload.
 */
PERF_TEST(TcpSocketPerf, EchoLatencySmall) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD_SIZE = 64;
  const std::string IP = "127.0.0.1";
  const std::string PORT = "19100";

  EchoServerFixture server(IP, PORT);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  TcpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::array<uint8_t, 128> echoBuf{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::string writeErr, readErr;
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000, writeErr);
      (void)client.read(echoBuf, 1000, readErr);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        std::string writeErr, readErr;
        ssize_t nw =
            client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000, writeErr);
        ASSERT_GT(nw, 0) << writeErr;
        ssize_t nr = client.read(echoBuf, 1000, readErr);
        ASSERT_GT(nr, 0) << readErr;
      },
      "echo_64B");

  std::printf("\n[EchoLatencySmall] %zu bytes\n", PAYLOAD_SIZE);
  std::printf("  Round-trip: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f echo/sec\n", result.callsPerSecond);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief TCP echo round-trip latency with medium (1 KB) payload.
 */
PERF_TEST(TcpSocketPerf, EchoLatencyMedium) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD_SIZE = 1024;
  const std::string IP = "127.0.0.1";
  const std::string PORT = "19101";

  EchoServerFixture server(IP, PORT);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  TcpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::vector<uint8_t> echoBuf(PAYLOAD_SIZE * 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::string writeErr, readErr;
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000, writeErr);
      (void)client.read(apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), 1000,
                        readErr);
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = PAYLOAD_SIZE,
      .bytesWritten = PAYLOAD_SIZE,
      .bytesAllocated = 0,
  };

  auto result = perf.throughputLoop(
      [&] {
        std::string writeErr, readErr;
        ssize_t nw =
            client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000, writeErr);
        ASSERT_GT(nw, 0) << writeErr;
        ssize_t nr = client.read(apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()),
                                 1000, readErr);
        ASSERT_GT(nr, 0) << readErr;
      },
      "echo_1KB", memProfile);

  std::printf("\n[EchoLatencyMedium] %zu bytes\n", PAYLOAD_SIZE);
  std::printf("  Round-trip: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f echo/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE * 2) / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief TCP echo round-trip latency with large (16 KB) payload.
 */
PERF_TEST(TcpSocketPerf, EchoLatencyLarge) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD_SIZE = static_cast<const size_t>(16 * 1024);
  const std::string IP = "127.0.0.1";
  const std::string PORT = "19102";

  EchoServerFixture server(IP, PORT);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  TcpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::vector<uint8_t> echoBuf(PAYLOAD_SIZE * 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::string writeErr, readErr;
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000, writeErr);
      size_t totalRead = 0;
      while (totalRead < PAYLOAD_SIZE) {
        ssize_t nr = client.read(apex::compat::mutable_bytes_span(echoBuf.data() + totalRead,
                                                                  echoBuf.size() - totalRead),
                                 1000, readErr);
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
        std::string writeErr, readErr;
        ssize_t nw =
            client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000, writeErr);
        ASSERT_GT(nw, 0) << writeErr;

        size_t totalRead = 0;
        while (totalRead < PAYLOAD_SIZE) {
          ssize_t nr = client.read(apex::compat::mutable_bytes_span(echoBuf.data() + totalRead,
                                                                    echoBuf.size() - totalRead),
                                   1000, readErr);
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
}

/* ----------------------------- Throughput ----------------------------- */

/**
 * @brief RT-safe drain callback context for throughput test.
 */
struct TcpDrainContext {
  TcpSocketServer* server;
};

void tcpDrainCallback(void* ctx, int clientfd) noexcept {
  auto* drainctx = static_cast<TcpDrainContext*>(ctx);
  std::array<uint8_t, 8192> buf{};
  while (drainctx->server->read(clientfd, buf, 0) > 0) {
  }
}

/**
 * @brief Maximum write throughput (fire-and-forget pattern).
 */
PERF_TEST(TcpSocketPerf, WriteThroughput) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD_SIZE = 1024;
  const std::string IP = "127.0.0.1";
  const std::string PORT = "19103";

  TcpSocketServer server(IP, PORT);
  std::string srvErr;
  ASSERT_EQ(server.init(srvErr), TCP_SERVER_SUCCESS) << srvErr;

  TcpDrainContext drainCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{tcpDrainCallback, &drainCtx});

  std::atomic_bool running{true};
  std::thread ioThread([&]() {
    while (running.load(std::memory_order_relaxed)) {
      server.processEvents(50);
    }
  });
  std::this_thread::sleep_for(20ms);

  TcpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;

  auto payload = generatePayload(PAYLOAD_SIZE);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::string writeErr;
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000, writeErr);
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
        std::string writeErr;
        sink =
            client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000, writeErr);
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
}

/* ----------------------------- Multi-Client ----------------------------- */

/**
 * @brief Server performance under concurrent client load.
 */
PERF_TEST(TcpSocketPerf, MultiClientEcho) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD_SIZE = 256;
  constexpr size_t NUM_CLIENTS = 4;
  const std::string IP = "127.0.0.1";
  const std::string PORT = "19104";

  EchoServerFixture server(IP, PORT);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  std::vector<std::unique_ptr<TcpSocketClient>> clients;
  for (size_t i = 0; i < NUM_CLIENTS; ++i) {
    auto client = std::make_unique<TcpSocketClient>(IP, PORT);
    std::string cliErr;
    ASSERT_EQ(client->init(1000, cliErr), TCP_CLIENT_SUCCESS) << "Client " << i << ": " << cliErr;
    clients.push_back(std::move(client));
  }

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::vector<uint8_t> echoBuf(PAYLOAD_SIZE * 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (auto& client : clients) {
        std::string writeErr, readErr;
        (void)client->write(apex::compat::bytes_span(payload.data(), payload.size()), 1000,
                            writeErr);
        (void)client->read(apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), 1000,
                           readErr);
      }
    }
  });

  size_t clientIdx = 0;
  auto result = perf.throughputLoop(
      [&] {
        auto& client = clients[clientIdx];
        clientIdx = (clientIdx + 1) % NUM_CLIENTS;

        std::string writeErr, readErr;
        ssize_t nw =
            client->write(apex::compat::bytes_span(payload.data(), payload.size()), 1000, writeErr);
        ASSERT_GT(nw, 0) << writeErr;
        ssize_t nr = client->read(apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()),
                                  1000, readErr);
        ASSERT_GT(nr, 0) << readErr;
      },
      "multi_client");

  std::printf("\n[MultiClientEcho] %zu clients, %zu bytes\n", NUM_CLIENTS, PAYLOAD_SIZE);
  std::printf("  Round-trip: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Aggregate throughput: %.0f echo/sec\n", result.callsPerSecond);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/* ----------------------------- Payload Scaling ----------------------------- */

/**
 * @brief Latency comparison across payload sizes.
 */
PERF_TEST(TcpSocketPerf, PayloadScaling) {
  UB_PERF_GUARD(perf);

  const std::string IP = "127.0.0.1";
  const std::string PORT = "19105";

  EchoServerFixture server(IP, PORT);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  TcpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;

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
      std::string writeErr, readErr;
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000, writeErr);
      size_t total = 0;
      while (total < test.size) {
        ssize_t nr = client.read(
            apex::compat::mutable_bytes_span(echoBuf.data() + total, echoBuf.size() - total), 1000,
            readErr);
        if (nr <= 0)
          break;
        total += static_cast<size_t>(nr);
      }
    }

    ub::PerfConfig cfg = ub::detail::getPerfConfig();
    cfg.msgBytes = static_cast<int>(test.size);
    std::string testName = "TcpSocketPerf.PayloadScaling/" + std::string(test.name);
    ub::PerfCase subPerf{testName, cfg};

    auto result = subPerf.throughputLoop(
        [&] {
          std::string writeErr, readErr;
          (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000,
                             writeErr);
          size_t total = 0;
          while (total < test.size) {
            ssize_t nr = client.read(
                apex::compat::mutable_bytes_span(echoBuf.data() + total, echoBuf.size() - total),
                1000, readErr);
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
}

PERF_MAIN()
