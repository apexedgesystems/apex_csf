/**
 * @file UdpSocket_pTest.cpp
 * @brief Performance tests for UDP socket server/client echo operations.
 *
 * Measures:
 *  - Echo round-trip latency at 64 B and 1 KB (CONNECTED mode)
 *  - Echo round-trip latency at 64 B (UNCONNECTED mode)
 *  - Send throughput (fire-and-forget)
 *  - CONNECTED vs UNCONNECTED mode comparison
 *  - Payload size scaling
 *
 * Usage:
 *   ./UdpSocket_PTEST --csv results.csv
 *   ./UdpSocket_PTEST --quick
 *   ./UdpSocket_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpSocketClient.hpp"
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpSocketServer.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace ub = vernier::bench;
using namespace std::chrono_literals;

using apex::protocols::udp::UDP_CLIENT_SUCCESS;
using apex::protocols::udp::UDP_SERVER_SUCCESS;
using apex::protocols::udp::UdpSocketClient;
using apex::protocols::udp::UdpSocketMode;
using apex::protocols::udp::UdpSocketServer;

/* ----------------------------- Test Helpers ----------------------------- */

namespace {

ssize_t waitAndRead(UdpSocketClient& client, apex::compat::mutable_bytes_span buf, int timeoutMs) {
  if (!client.waitReadable(timeoutMs))
    return 0;
  return client.read(buf);
}

ssize_t waitAndReadFrom(UdpSocketClient& client, apex::compat::mutable_bytes_span buf,
                        UdpSocketClient::PeerInfo& from, int timeoutMs) {
  if (!client.waitReadable(timeoutMs))
    return 0;
  return client.readFrom(buf, from);
}

bool resolvePeer(const std::string& host, const std::string& port, UdpSocketClient::PeerInfo& out,
                 std::string& err) {
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  struct addrinfo* addrs = nullptr;
  int rv = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &addrs);
  if (rv != 0) {
    err = std::string("getaddrinfo failed: ") + ::gai_strerror(rv);
    return false;
  }
  bool ok = false;
  for (auto* ai = addrs; ai != nullptr; ai = ai->ai_next) {
    if (ai->ai_addrlen <= sizeof(out.addr)) {
      std::memcpy(&out.addr, ai->ai_addr, ai->ai_addrlen);
      out.addrLen = static_cast<socklen_t>(ai->ai_addrlen);
      ok = true;
      break;
    }
  }
  ::freeaddrinfo(addrs);
  if (!ok)
    err = "no usable address from getaddrinfo";
  return ok;
}

/**
 * @brief RAII echo server for UDP performance tests.
 *
 * Uses RT-safe Delegate callback (no heap allocation) for datagram handling.
 */
class UdpEchoServerFixture {
public:
  UdpEchoServerFixture(const std::string& ip, const std::string& port) : server_(ip, port) {}

  bool start(std::string& error) {
    uint8_t status = server_.init(error);
    if (status != UDP_SERVER_SUCCESS)
      return false;

    server_.setOnDatagramReceived(apex::concurrency::Delegate<void>{echoCallback, this});

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

  ~UdpEchoServerFixture() { stop(); }

private:
  static void echoCallback(void* ctx) noexcept {
    auto* self = static_cast<UdpEchoServerFixture*>(ctx);
    std::array<uint8_t, 2048> buf{};
    for (;;) {
      auto info = self->server_.read(buf, 0);
      if (info.nread <= 0)
        break;
      apex::compat::bytes_span out(buf.data(), static_cast<size_t>(info.nread));
      std::string writeErr;
      (void)self->server_.write(info, out, 0, writeErr);
    }
  }

  UdpSocketServer server_;
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
 * @brief UDP echo round-trip latency in CONNECTED mode.
 */
PERF_TEST(UdpSocketPerf, EchoLatencyConnected) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD_SIZE = 64;
  const std::string IP = "127.0.0.1";
  const std::string PORT = "19200";

  UdpEchoServerFixture server(IP, PORT);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  UdpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliErr), UDP_CLIENT_SUCCESS) << cliErr;

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::array<uint8_t, 256> echoBuf{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0);
      (void)waitAndRead(client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()),
                        100);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        ssize_t nw = client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0);
        ASSERT_GT(nw, 0) << "Write failed";

        ssize_t nr = waitAndRead(
            client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), 100);
        ASSERT_GT(nr, 0) << "Echo timeout";
      },
      "echo_connected_64B");

  std::printf("\n[EchoLatencyConnected] %zu bytes\n", PAYLOAD_SIZE);
  std::printf("  Round-trip: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f echo/sec\n", result.callsPerSecond);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief UDP echo round-trip latency in UNCONNECTED mode.
 */
PERF_TEST(UdpSocketPerf, EchoLatencyUnconnected) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD_SIZE = 64;
  const std::string IP = "127.0.0.1";
  const std::string PORT = "19201";

  UdpEchoServerFixture server(IP, PORT);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  UdpSocketClient client("", "0");
  std::string cliErr;
  ASSERT_EQ(client.init(UdpSocketMode::UNCONNECTED, cliErr), UDP_CLIENT_SUCCESS) << cliErr;

  UdpSocketClient::PeerInfo srvPeer{};
  std::string resErr;
  ASSERT_TRUE(resolvePeer(IP, PORT, srvPeer, resErr)) << resErr;

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::array<uint8_t, 256> echoBuf{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)client.writeTo(apex::compat::bytes_span(payload.data(), payload.size()), srvPeer, 0);
      UdpSocketClient::PeerInfo from{};
      (void)waitAndReadFrom(
          client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), from, 100);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        ssize_t nw =
            client.writeTo(apex::compat::bytes_span(payload.data(), payload.size()), srvPeer, 0);
        ASSERT_GT(nw, 0) << "Write failed";

        UdpSocketClient::PeerInfo from{};
        ssize_t nr = waitAndReadFrom(
            client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), from, 100);
        ASSERT_GT(nr, 0) << "Echo timeout";
      },
      "echo_unconnected_64B");

  std::printf("\n[EchoLatencyUnconnected] %zu bytes\n", PAYLOAD_SIZE);
  std::printf("  Round-trip: %.3f us (median), %.3f us (p10), %.3f us (p90)\n", result.stats.median,
              result.stats.p10, result.stats.p90);
  std::printf("  Throughput: %.0f echo/sec\n", result.callsPerSecond);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief UDP echo with 1 KB datagram payload.
 */
PERF_TEST(UdpSocketPerf, EchoLatencyMedium) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD_SIZE = 1024;
  const std::string IP = "127.0.0.1";
  const std::string PORT = "19202";

  UdpEchoServerFixture server(IP, PORT);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  UdpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliErr), UDP_CLIENT_SUCCESS) << cliErr;

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::vector<uint8_t> echoBuf(PAYLOAD_SIZE * 2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0);
      (void)waitAndRead(client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()),
                        100);
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = PAYLOAD_SIZE,
      .bytesWritten = PAYLOAD_SIZE,
      .bytesAllocated = 0,
  };

  auto result = perf.throughputLoop(
      [&] {
        ssize_t nw = client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0);
        ASSERT_GT(nw, 0) << "Write failed";

        ssize_t nr = waitAndRead(
            client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), 100);
        ASSERT_GT(nr, 0) << "Echo timeout";
      },
      "echo_1KB", memProfile);

  std::printf("\n[EchoLatencyMedium] %zu bytes\n", PAYLOAD_SIZE);
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
struct DrainContext {
  UdpSocketServer* server;
};

void drainCallback(void* ctx) noexcept {
  auto* drainctx = static_cast<DrainContext*>(ctx);
  std::array<uint8_t, 2048> buf{};
  while (drainctx->server->read(buf, 0).nread > 0) {
  }
}

/**
 * @brief Maximum UDP send throughput (fire-and-forget).
 */
PERF_TEST(UdpSocketPerf, SendThroughput) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD_SIZE = 512;
  const std::string IP = "127.0.0.1";
  const std::string PORT = "19203";

  UdpSocketServer server(IP, PORT);
  std::string srvErr;
  ASSERT_EQ(server.init(srvErr), UDP_SERVER_SUCCESS) << srvErr;

  DrainContext drainCtx{&server};
  server.setOnDatagramReceived(apex::concurrency::Delegate<void>{drainCallback, &drainCtx});

  std::atomic_bool running{true};
  std::thread ioThread([&]() {
    while (running.load(std::memory_order_relaxed)) {
      server.processEvents(50);
    }
  });
  std::this_thread::sleep_for(20ms);

  UdpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliErr), UDP_CLIENT_SUCCESS) << cliErr;

  auto payload = generatePayload(PAYLOAD_SIZE);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0);
    }
  });

  ub::MemoryProfile memProfile{
      .bytesRead = 0,
      .bytesWritten = PAYLOAD_SIZE,
      .bytesAllocated = 0,
  };

  volatile ssize_t sink = 0;
  auto result = perf.throughputLoop(
      [&] { sink = client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0); },
      "send_512B", memProfile);

  std::printf("\n[SendThroughput] %zu bytes per datagram\n", PAYLOAD_SIZE);
  std::printf("  Latency: %.3f us (median)\n", result.stats.median);
  std::printf("  Throughput: %.0f dgram/sec, %.2f MB/s\n", result.callsPerSecond,
              (result.callsPerSecond * PAYLOAD_SIZE) / 1e6);

  running = false;
  server.stop();
  ioThread.join();

  (void)sink;
}

/* ----------------------------- Mode Comparison ----------------------------- */

/**
 * @brief CONNECTED vs UNCONNECTED mode performance comparison.
 */
PERF_TEST(UdpSocketPerf, ModeComparison) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD_SIZE = 256;
  const std::string IP = "127.0.0.1";
  const std::string PORT = "19204";

  UdpEchoServerFixture server(IP, PORT);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  auto payload = generatePayload(PAYLOAD_SIZE);
  std::array<uint8_t, 512> echoBuf{};

  {
    UdpSocketClient client(IP, PORT);
    std::string cliErr;
    ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliErr), UDP_CLIENT_SUCCESS) << cliErr;

    ub::PerfConfig cfg = ub::detail::getPerfConfig();
    ub::PerfCase connPerf{"UdpSocketPerf.ModeComparison/CONNECTED", cfg};

    connPerf.warmup([&] {
      for (int i = 0; i < connPerf.cycles(); ++i) {
        (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0);
        (void)waitAndRead(client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()),
                          100);
      }
    });

    auto connResult = connPerf.throughputLoop(
        [&] {
          (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0);
          (void)waitAndRead(client,
                            apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), 100);
        },
        "connected");

    std::printf("\n[ModeComparison] %zu bytes\n", PAYLOAD_SIZE);
    std::printf("  CONNECTED:   %.3f us (median), %.0f echo/sec\n", connResult.stats.median,
                connResult.callsPerSecond);
  }

  {
    UdpSocketClient client("", "0");
    std::string cliErr;
    ASSERT_EQ(client.init(UdpSocketMode::UNCONNECTED, cliErr), UDP_CLIENT_SUCCESS) << cliErr;

    UdpSocketClient::PeerInfo srvPeer{};
    std::string resErr;
    ASSERT_TRUE(resolvePeer(IP, PORT, srvPeer, resErr)) << resErr;

    ub::PerfConfig cfg = ub::detail::getPerfConfig();
    ub::PerfCase unconPerf{"UdpSocketPerf.ModeComparison/UNCONNECTED", cfg};

    unconPerf.warmup([&] {
      for (int i = 0; i < unconPerf.cycles(); ++i) {
        (void)client.writeTo(apex::compat::bytes_span(payload.data(), payload.size()), srvPeer, 0);
        UdpSocketClient::PeerInfo from{};
        (void)waitAndReadFrom(
            client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), from, 100);
      }
    });

    auto unconResult = unconPerf.throughputLoop(
        [&] {
          (void)client.writeTo(apex::compat::bytes_span(payload.data(), payload.size()), srvPeer,
                               0);
          UdpSocketClient::PeerInfo from{};
          (void)waitAndReadFrom(
              client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), from, 100);
        },
        "unconnected");

    std::printf("  UNCONNECTED: %.3f us (median), %.0f echo/sec\n", unconResult.stats.median,
                unconResult.callsPerSecond);
  }
}

/* ----------------------------- Payload Scaling ----------------------------- */

/**
 * @brief Latency comparison across datagram payload sizes.
 */
PERF_TEST(UdpSocketPerf, PayloadScaling) {
  UB_PERF_GUARD(perf);

  const std::string IP = "127.0.0.1";
  const std::string PORT = "19205";

  UdpEchoServerFixture server(IP, PORT);
  std::string srvErr;
  ASSERT_TRUE(server.start(srvErr)) << srvErr;

  UdpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliErr), UDP_CLIENT_SUCCESS) << cliErr;

  struct TestCase {
    const char* name;
    size_t size;
  };

  std::vector<TestCase> tests = {{"32B", 32},   {"64B", 64},   {"128B", 128},  {"256B", 256},
                                 {"512B", 512}, {"1KB", 1024}, {"1400B", 1400}};

  std::printf("\n%-8s %-12s %-15s %-12s\n", "Size", "Latency(us)", "Throughput", "MB/s");
  std::printf("%s\n", std::string(50, '-').c_str());

  for (const auto& test : tests) {
    auto payload = generatePayload(test.size);
    std::vector<uint8_t> echoBuf(test.size * 2);

    for (int i = 0; i < 100; ++i) {
      (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0);
      (void)waitAndRead(client, apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()),
                        100);
    }

    ub::PerfConfig cfg = ub::detail::getPerfConfig();
    cfg.msgBytes = static_cast<int>(test.size);
    std::string testName = "UdpSocketPerf.PayloadScaling/" + std::string(test.name);
    ub::PerfCase subPerf{testName, cfg};

    auto result = subPerf.throughputLoop(
        [&] {
          (void)client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0);
          (void)waitAndRead(client,
                            apex::compat::mutable_bytes_span(echoBuf.data(), echoBuf.size()), 100);
        },
        test.name);

    double mbPerSec = (result.callsPerSecond * test.size * 2) / 1e6;
    std::printf("%-8s %-12.1f %-15.0f %-12.1f\n", test.name, result.stats.median,
                result.callsPerSecond, mbPerSec);
  }
}

PERF_MAIN()
